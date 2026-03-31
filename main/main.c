#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <sys/time.h>
#include "my_rtc.h" // 這是您 components/rtc 裡面的標頭檔
#include "wifi.h"
#include "display.h"
#include "button.h"
#include "lvgl.h"
#include "esp_sntp.h" // 新增 SNTP 標頭檔
#include <time.h>
#include <sys/time.h>

static const char *TAG = "MAIN";

static bool is_setting_time = false;
static struct tm time_setting = {0};
static int current_panel = 0;

// 全局 LVGL標籤，用來顯示時鐘
lv_obj_t *time_label;

// LVGL 的時間滴答
static void lvgl_tick_cb(void *arg) { lv_tick_inc(2); }

// LVGL 背景任務
static void lvgl_task(void *arg)
{
    while (1)
    {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// 按鈕回調函數
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
                // 保存時間設置
                time_t now = mktime(&time_setting);
                struct timeval tv = {
                    .tv_sec = now,
                    .tv_usec = 0};
                settimeofday(&tv, NULL);
                rtc_save_to_nvs();
                ESP_LOGI(TAG, "時間設置已保存");
            }
            else
            {
                ESP_LOGI(TAG, "進入時間設置模式");
            }
            break;
        }
    }
}

void app_main(void)
{
    // 初始化 NVS
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

    // 初始化所有模組
    ESP_LOGI(TAG, "初始化顯示模組...");

    display_init(); // 內部必須呼叫 display_lvgl_init

    ESP_LOGI(TAG, "初始化按鈕模組...");
    button_init();
    button_register_callback(button_event_callback);

    ESP_LOGI(TAG, "初始化 RTC 模組...");
    my_rtc_init();

    // 首先嘗試從 NVS 加載上次的時間
    ESP_LOGI(TAG, "從 NVS 加載上次的時間...");
    rtc_load_from_nvs();

    ESP_LOGI(TAG, "初始化 WIFI 模組...");
    wifi_init();

    // 等待 WIFI 連接並同步時間
    ESP_LOGI(TAG, "嘗試通過 WIFI 同步時間...");
    if (wifi_connect("RexHsu", "0933356554"))
    // if (wifi_connect("GGININDER_24G_5G", "9876543210"))
    {
        ESP_LOGI(TAG, "WIFI 連接成功，正在同步時間...");
        rtc_sync_from_ntp();
    }
    else
    {
        ESP_LOGW(TAG, "WIFI 連接失敗，使用本地時間");
    }

    // --- 請在這裡加入時區設定 ---
    ESP_LOGI(TAG, "設定本地時區為 UTC+8 (台灣時間)");
    setenv("TZ", "CST-8", 1);
    tzset();

    // --- 加入除錯訊息：印出當前系統時間 ---
    time_t debug_now = time(NULL);
    struct tm *debug_tm_info = localtime(&debug_now);
    char debug_time_str[64];
    strftime(debug_time_str, sizeof(debug_time_str), "%Y-%m-%d %H:%M:%S", debug_tm_info);
    ESP_LOGW(TAG, "【除錯】連線與對時後，系統現在的時間為: %s", debug_time_str);
    // ------------------------------------

    // 初始化 LVGL Timer
    const esp_timer_create_args_t tick_timer_args = {.callback = lvgl_tick_cb, .name = "lvgl_tick"};
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_timer_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 2 * 1000));

    // 啟動 LVGL 背景任務
    xTaskCreate(lvgl_task, "lvgl", 4096, NULL, 5, NULL);

    // --- 使用 LVGL 畫一個大大的時鐘標籤 ---
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0); // 黑底

    time_label = lv_label_create(scr);
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xFF0000), 0); // 紅字
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_24, 0);  // 字體大小
    lv_obj_center(time_label);                                          // 居中

    // 定時保存時間到 NVS（每分鐘一次）
    // uint32_t save_counter = 0;

    ESP_LOGI(TAG, "進入主迴圈");
    // gpio_set_level(PIN_BLK, 1);
    display_set_brightness(100);

    // 主迴圈只要更新標籤內容即可！
    while (1)
    {
        time_t now = time(NULL);
        struct tm *timeinfo = localtime(&now);

        // 使用 LVGL API 自動更新時間顯示
        lv_label_set_text_fmt(time_label, "%02d:%02d:%02d",
                              timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}