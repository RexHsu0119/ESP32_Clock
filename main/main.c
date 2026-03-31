#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "my_rtc.h"
#include "wifi.h"
#include "display.h"
#include "button.h"
#include "lvgl.h"

static const char *TAG = "MAIN";

/* 建議之後改成從設定檔或 NVS 讀取，不要硬編碼在原始碼 */
static const char *WIFI_SSID = "RexHsu";
static const char *WIFI_PASSWORD = "0933356554";

static bool is_setting_time = false;
static struct tm time_setting = {0};
static int current_panel = 0;

/* 全局 LVGL 標籤 */
static lv_obj_t *time_label = NULL;
static lv_obj_t *status_label = NULL;

/* LVGL 互斥鎖 */
static SemaphoreHandle_t lvgl_mutex = NULL;

/* 背景同步狀態 */
static volatile bool g_time_syncing = false;
static volatile bool g_wifi_failed = false;

/* LVGL 的時間滴答 */
static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(2);
}

/* 更新畫面上的時間與狀態 */
static void update_ui(void)
{
    struct tm display_time;

    if (is_setting_time)
    {
        display_time = time_setting;
    }
    else
    {
        time_t now = time(NULL);
        if (localtime_r(&now, &display_time) == NULL)
        {
            return;
        }
    }

    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        if (time_label != NULL)
        {
            lv_label_set_text_fmt(time_label, "%02d:%02d:%02d",
                                  display_time.tm_hour,
                                  display_time.tm_min,
                                  display_time.tm_sec);
        }

        if (status_label != NULL)
        {
            if (is_setting_time)
            {
                lv_label_set_text(status_label, "Setting Hour");
            }
            else if (g_time_syncing)
            {
                lv_label_set_text(status_label, "Syncing...");
            }
            else if (g_wifi_failed)
            {
                lv_label_set_text(status_label, "Offline");
            }
            else
            {
                lv_label_set_text(status_label, "");
            }
        }

        xSemaphoreGive(lvgl_mutex);
    }
}

/* LVGL 背景任務 */
static void lvgl_task(void *arg)
{
    (void)arg;

    while (1)
    {
        if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            lv_timer_handler();
            xSemaphoreGive(lvgl_mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* 背景網路 + NTP 同步任務 */
static void network_time_task(void *arg)
{
    (void)arg;

    g_time_syncing = true;
    g_wifi_failed = false;
    update_ui();

    ESP_LOGI(TAG, "背景開始進行 Wi-Fi 與 NTP 校時");

    if (wifi_connect(WIFI_SSID, WIFI_PASSWORD))
    {
        ESP_LOGI(TAG, "WIFI 連接成功，正在同步時間...");
        rtc_sync_from_ntp();
    }
    else
    {
        g_wifi_failed = true;
        ESP_LOGW(TAG, "WIFI 連接失敗，使用本地/NVS時間");
    }

    g_time_syncing = false;
    update_ui();

    ESP_LOGI(TAG, "背景網路校時流程結束");
    vTaskDelete(NULL);
}

/* 按鈕回調函數 */
void button_event_callback(uint8_t button_id, uint8_t event_type)
{
    if (event_type == BUTTON_SHORT_PRESS)
    {
        switch (button_id)
        {
        case BUTTON_UP:
            if (is_setting_time)
            {
                time_setting.tm_hour = (time_setting.tm_hour + 1) % 24;
                ESP_LOGI(TAG, "時間設置 - 小時: %d", time_setting.tm_hour);
            }
            else
            {
                current_panel = (current_panel + 1) % 3;
                ESP_LOGI(TAG, "切換面板: %d", current_panel);
            }
            break;

        case BUTTON_DOWN:
            if (is_setting_time)
            {
                time_setting.tm_hour = (time_setting.tm_hour - 1 + 24) % 24;
                ESP_LOGI(TAG, "時間設置 - 小時: %d", time_setting.tm_hour);
            }
            else
            {
                current_panel = (current_panel - 1 + 3) % 3;
                ESP_LOGI(TAG, "切換面板: %d", current_panel);
            }
            break;

        case BUTTON_CENTER:
            break;
        }
    }
    else if (event_type == BUTTON_LONG_PRESS)
    {
        switch (button_id)
        {
        case BUTTON_CENTER:
            is_setting_time = !is_setting_time;

            if (!is_setting_time)
            {
                /* 離開設定模式，保存時間 */
                time_setting.tm_isdst = -1;

                time_t new_time = mktime(&time_setting);
                if (new_time != (time_t)-1)
                {
                    struct timeval tv = {
                        .tv_sec = new_time,
                        .tv_usec = 0};
                    settimeofday(&tv, NULL);
                    rtc_save_to_nvs();
                    ESP_LOGI(TAG, "時間設置已保存");
                }
                else
                {
                    ESP_LOGE(TAG, "mktime 失敗，未保存時間");
                }
            }
            else
            {
                /* 進入設定模式，先載入目前系統時間 */
                time_t now = time(NULL);
                if (localtime_r(&now, &time_setting) != NULL)
                {
                    ESP_LOGI(TAG, "進入時間設置模式");
                }
                else
                {
                    ESP_LOGW(TAG, "讀取目前系統時間失敗");
                    time_setting = (struct tm){0};
                }
            }
            break;
        }
    }

    /* 讓設定狀態與時間顯示立即更新 */
    update_ui();
}

void app_main(void)
{
    /* 初始化 NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "ESP32-S3 時鐘顯示系統啟動");
    ESP_LOGI(TAG, "========================================");

    /* 初始化顯示 */
    ESP_LOGI(TAG, "初始化顯示模組...");
    display_init();

    /* 建立 LVGL mutex */
    lvgl_mutex = xSemaphoreCreateMutex();
    if (lvgl_mutex == NULL)
    {
        ESP_LOGE(TAG, "建立 LVGL mutex 失敗");
        return;
    }

    /* 初始化按鈕 */
    ESP_LOGI(TAG, "初始化按鈕模組...");
    button_init();
    button_register_callback(button_event_callback);

    /* 初始化 RTC */
    ESP_LOGI(TAG, "初始化 RTC 模組...");
    my_rtc_init();

    /* 先從 NVS 載入上次時間 */
    ESP_LOGI(TAG, "從 NVS 載入上次的時間...");
    rtc_load_from_nvs();

    /* 初始化 LVGL Timer */
    const esp_timer_create_args_t tick_timer_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick"};
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_timer_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 2 * 1000));

    /* 建立畫面 */
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        lv_obj_t *scr = lv_screen_active();
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

        time_label = lv_label_create(scr);
        lv_obj_set_style_text_color(time_label, lv_color_hex(0xFF0000), 0);
        lv_obj_set_style_text_font(time_label, &lv_font_montserrat_24, 0);
        lv_obj_align(time_label, LV_ALIGN_CENTER, 0, -10);

        status_label = lv_label_create(scr);
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
        lv_label_set_text(status_label, "Syncing...");
        lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 18);

        xSemaphoreGive(lvgl_mutex);
    }

    /* 先顯示 NVS 載入後的時間 */
    g_time_syncing = true;
    g_wifi_failed = false;
    update_ui();

    /* 啟動 LVGL 背景任務 */
    xTaskCreate(lvgl_task, "lvgl", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "進入主迴圈");
    display_set_brightness(100);

    /* 初始化 WIFI，成功後改由背景任務連線與 NTP 校時 */
    ESP_LOGI(TAG, "初始化 WIFI 模組...");
    if (wifi_init())
    {
        g_time_syncing = true;
        g_wifi_failed = false;
        xTaskCreate(network_time_task, "network_time_task", 4096, NULL, 5, NULL);
    }
    else
    {
        g_time_syncing = false;
        g_wifi_failed = true;
        ESP_LOGW(TAG, "WIFI 初始化失敗，使用本地/NVS時間");
        update_ui();
    }

    /* 主迴圈每秒更新一次時間 */
    while (1)
    {
        update_ui();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
