#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "esp_attr.h"

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/i2s_std.h"

#include "my_rtc.h"
#include "wifi.h"
#include "wifi_config.h"
#include "wifi_portal.h"
#include "display.h"
#include "button.h"
#include "weather.h"
#include "lvgl.h"

static const char *TAG = "MAIN";

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DEGREE_UTF8 "\xC2\xB0"

typedef enum
{
    PANEL_DIGITAL = 0,
    PANEL_ANALOG,
} clock_panel_t;

typedef enum
{
    SET_FIELD_HOUR = 0,
    SET_FIELD_MINUTE,
    SET_FIELD_SECOND,
} time_set_field_t;

typedef enum
{
    APP_MODE_CLOCK = 0,
    APP_MODE_WIFI_PORTAL,
} app_mode_t;

typedef enum
{
    BOOT_HINT_NONE = 0,
    BOOT_HINT_FORCE_SETUP,
    BOOT_HINT_CLEAR_WIFI,
} boot_hint_t;

typedef enum
{
    ALARM_REPEAT_ONCE = 0,
    ALARM_REPEAT_DAILY,
} alarm_repeat_t;

typedef enum
{
    ALARM_FIELD_ENABLE = 0,
    ALARM_FIELD_REPEAT,
    ALARM_FIELD_HOUR,
    ALARM_FIELD_MINUTE,
} alarm_set_field_t;

typedef struct
{
    bool enabled;
    int hour;
    int minute;
    alarm_repeat_t repeat;
} alarm_config_t;

static bool is_setting_time = false;
static struct tm time_setting = {0};
static clock_panel_t current_panel = PANEL_DIGITAL;
static time_set_field_t current_set_field = SET_FIELD_HOUR;
static app_mode_t g_app_mode = APP_MODE_CLOCK;
static boot_hint_t g_boot_hint = BOOT_HINT_NONE;

/* 鬧鐘設定/狀態 */
static alarm_config_t g_alarm = {
    .enabled = false,
    .hour = 7,
    .minute = 0,
    .repeat = ALARM_REPEAT_DAILY,
};
static alarm_config_t g_alarm_edit = {0};
static bool g_alarm_setting_mode = false;
static alarm_set_field_t g_alarm_set_field = ALARM_FIELD_ENABLE;
static volatile bool g_alarm_ringing = false;
static bool g_alarm_flash_on = false;
static int64_t g_alarm_last_flash_us = 0;
static TaskHandle_t g_alarm_sound_task_handle = NULL;

/* 避免同一分鐘重複觸發 */
static int g_alarm_last_trigger_year = -1;
static int g_alarm_last_trigger_yday = -1;
static int g_alarm_last_trigger_hour = -1;
static int g_alarm_last_trigger_minute = -1;

/* 時間顯示有效旗標 */
static bool g_time_base_valid = false;

/* 是否在同步期間強制顯示未知值 */
static bool g_force_unknown_during_sync = true;

/* Deep Sleep 後仍保留的狀態 */
RTC_DATA_ATTR static bool s_rtc_time_valid = false;

/* 由 main task 執行 Deep Sleep，避免在 button task 內做重操作 */
static volatile bool g_request_deep_sleep = false;

/* Wi-Fi credentials 由 NVS 載入 */
static char g_wifi_ssid[WIFI_CONFIG_SSID_MAX_LEN + 1] = {0};
static char g_wifi_password[WIFI_CONFIG_PASSWORD_MAX_LEN + 1] = {0};
static bool g_wifi_credentials_loaded = false;

/* LVGL 物件 */
static lv_obj_t *digital_container = NULL;
static lv_obj_t *analog_container = NULL;
static lv_obj_t *portal_container = NULL;

/* 開機提示 overlay */
static lv_obj_t *boot_overlay = NULL;
static lv_obj_t *boot_overlay_title = NULL;
static lv_obj_t *boot_overlay_line1 = NULL;
static lv_obj_t *boot_overlay_line2 = NULL;
static lv_obj_t *boot_overlay_line3 = NULL;
static lv_obj_t *boot_overlay_line4 = NULL;

/* 鬧鐘設定 overlay */
static lv_obj_t *alarm_overlay = NULL;
static lv_obj_t *alarm_title_label = NULL;
static lv_obj_t *alarm_help_label = NULL;
static lv_obj_t *alarm_hour_label = NULL;
static lv_obj_t *alarm_colon_label = NULL;
static lv_obj_t *alarm_minute_label = NULL;
static lv_obj_t *alarm_enable_label = NULL;
static lv_obj_t *alarm_repeat_label = NULL;

/* 數位錶面：上方列 */
static lv_obj_t *net_status_label = NULL;
static lv_obj_t *date_label = NULL;
static lv_obj_t *weekday_label = NULL;

/* 數位時鐘固定位置元件 */
static lv_obj_t *hour_label = NULL;
static lv_obj_t *minute_label = NULL;
static lv_obj_t *second_label = NULL;
static lv_obj_t *colon1_label = NULL;
static lv_obj_t *colon2_label = NULL;

/* 數位錶面：下方資訊列 */
static lv_obj_t *weather_label = NULL;

/* 類比錶面：上方列 */
static lv_obj_t *analog_status_label = NULL;
static lv_obj_t *analog_date_label = NULL;
static lv_obj_t *analog_weekday_label = NULL;

/* 類比錶面：底部單一資訊列 */
static lv_obj_t *analog_weather_label = NULL;

/* 配網畫面 */
static lv_obj_t *portal_title_label = NULL;
static lv_obj_t *portal_line1_label = NULL;
static lv_obj_t *portal_line2_label = NULL;
static lv_obj_t *portal_line3_label = NULL;
static lv_obj_t *portal_line4_label = NULL;
static lv_obj_t *portal_footer_label = NULL;

static lv_obj_t *analog_face = NULL;
static lv_obj_t *hour_hand = NULL;
static lv_obj_t *minute_hand = NULL;
static lv_obj_t *second_hand = NULL;
static lv_obj_t *center_dot = NULL;
static lv_obj_t *tick_marks[12] = {0};

/* 類比時鐘線段點 */
static lv_point_precise_t hour_points[2];
static lv_point_precise_t minute_points[2];
static lv_point_precise_t second_points[2];
static lv_point_precise_t tick_points[12][2];

/* LVGL 互斥鎖 */
static SemaphoreHandle_t lvgl_mutex = NULL;

/* 背景同步狀態 */
static volatile bool g_time_syncing = false;
static volatile bool g_wifi_failed = false;

/* MAX98357 I2S */
static i2s_chan_handle_t s_audio_tx_chan = NULL;
static bool s_audio_inited = false;

/* 類比時鐘尺寸：放大版 */
#define ANALOG_FACE_SIZE 76
#define ANALOG_CENTER_X (ANALOG_FACE_SIZE / 2)
#define ANALOG_CENTER_Y (ANALOG_FACE_SIZE / 2)
#define HOUR_HAND_LEN 19
#define MINUTE_HAND_LEN 28
#define SECOND_HAND_LEN 32

/* LVGL UI 更新週期 */
#define UI_UPDATE_PERIOD_NORMAL_MS 1000
#define UI_UPDATE_PERIOD_SETTING_MS 500
#define SETTING_BLINK_PERIOD_US 500000LL
#define ALARM_FLASH_PERIOD_US 400000LL

/* 數位時鐘固定欄位寬度 */
#define DIGIT_FIELD_WIDTH 40
#define COLON_FIELD_WIDTH 10

/* 類比錶面：置中錶盤 */
#define ANALOG_FACE_X ((DISPLAY_WIDTH - ANALOG_FACE_SIZE) / 2)
#define ANALOG_FACE_Y 22

/* 開機按住 UP 強制進 ClockSetup */
/* 開機按住 DOWN 清除 Wi-Fi 設定並進 ClockSetup */
#define PORTAL_FORCE_HOLD_MS 800
#define PORTAL_FORCE_SAMPLE_MS 20

/* 開機提示畫面顯示時間 */
#define BOOT_HINT_FORCE_SETUP_MS 2500
#define BOOT_HINT_CLEAR_WIFI_MS 2500

/* Wi-Fi portal 成功儲存後，保留提示畫面的顯示時間 */
#define PORTAL_SAVED_STATUS_MS 1500

/* MAX98357 pins on S3 智能擴展板 V1.7 */
#define AUDIO_I2S_BCLK_GPIO GPIO_NUM_15
#define AUDIO_I2S_WS_GPIO GPIO_NUM_16
#define AUDIO_I2S_DOUT_GPIO GPIO_NUM_7

#define AUDIO_SAMPLE_RATE_HZ 16000
#define AUDIO_TONE_FREQ_HZ 1000
#define AUDIO_AMPLITUDE 12000
#define AUDIO_FRAME_SAMPLES 256

#define ALARM_NAMESPACE "alarm_cfg"
#define ALARM_KEY_ENABLED "enabled"
#define ALARM_KEY_HOUR "hour"
#define ALARM_KEY_MINUTE "minute"
#define ALARM_KEY_REPEAT "repeat"

/* forward declarations */
static void update_ui(void);
static void network_time_task(void *arg);
static void update_alarm_overlay_locked(void);

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(2);
}

/* =========================
 * Audio / MAX98357
 * ========================= */
static void audio_i2s_init(void)
{
    if (s_audio_inited)
    {
        return;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_audio_tx_chan, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AUDIO_I2S_BCLK_GPIO,
            .ws = AUDIO_I2S_WS_GPIO,
            .dout = AUDIO_I2S_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_audio_tx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_audio_tx_chan));

    s_audio_inited = true;

    ESP_LOGI(TAG, "MAX98357 I2S 初始化完成: BCLK=%d WS=%d DOUT=%d",
             AUDIO_I2S_BCLK_GPIO, AUDIO_I2S_WS_GPIO, AUDIO_I2S_DOUT_GPIO);
}

static void audio_play_tone_ms(int freq_hz, int duration_ms)
{
    if (!s_audio_inited || s_audio_tx_chan == NULL)
    {
        return;
    }

    int16_t buffer[AUDIO_FRAME_SAMPLES * 2];
    size_t bytes_written = 0;

    const int total_samples = (AUDIO_SAMPLE_RATE_HZ * duration_ms) / 1000;
    int generated = 0;
    float phase = 0.0f;
    const float phase_step = 2.0f * (float)M_PI * (float)freq_hz / (float)AUDIO_SAMPLE_RATE_HZ;

    while (generated < total_samples)
    {
        int samples_this_round = AUDIO_FRAME_SAMPLES;
        if (samples_this_round > (total_samples - generated))
        {
            samples_this_round = total_samples - generated;
        }

        for (int i = 0; i < samples_this_round; i++)
        {
            int16_t sample = (int16_t)(sinf(phase) * AUDIO_AMPLITUDE);

            buffer[i * 2 + 0] = sample;
            buffer[i * 2 + 1] = sample;

            phase += phase_step;
            if (phase >= 2.0f * (float)M_PI)
            {
                phase -= 2.0f * (float)M_PI;
            }
        }

        ESP_ERROR_CHECK(i2s_channel_write(
            s_audio_tx_chan,
            buffer,
            samples_this_round * sizeof(int16_t) * 2,
            &bytes_written,
            portMAX_DELAY));

        generated += samples_this_round;
    }
}

static void audio_play_silence_ms(int duration_ms)
{
    if (!s_audio_inited || s_audio_tx_chan == NULL)
    {
        return;
    }

    int16_t buffer[AUDIO_FRAME_SAMPLES * 2];
    memset(buffer, 0, sizeof(buffer));

    size_t bytes_written = 0;
    const int total_samples = (AUDIO_SAMPLE_RATE_HZ * duration_ms) / 1000;
    int generated = 0;

    while (generated < total_samples)
    {
        int samples_this_round = AUDIO_FRAME_SAMPLES;
        if (samples_this_round > (total_samples - generated))
        {
            samples_this_round = total_samples - generated;
        }

        ESP_ERROR_CHECK(i2s_channel_write(
            s_audio_tx_chan,
            buffer,
            samples_this_round * sizeof(int16_t) * 2,
            &bytes_written,
            portMAX_DELAY));

        generated += samples_this_round;
    }
}

static void audio_play_beep_ms(int duration_ms)
{
    audio_i2s_init();
    audio_play_tone_ms(AUDIO_TONE_FREQ_HZ, duration_ms);
    audio_play_silence_ms(20);
}

/* =========================
 * Boot key / Wi-Fi creds
 * ========================= */
static bool is_button_held_on_boot(uint32_t gpio_num, const char *name)
{
    gpio_config_t io_conf = {0};
    io_conf.pin_bit_mask = (1ULL << gpio_num);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "設定開機偵測 %s 腳位失敗: %s", name, esp_err_to_name(ret));
        return false;
    }

    if (gpio_get_level(gpio_num) != 0)
    {
        return false;
    }

    ESP_LOGI(TAG, "偵測到開機時 %s 已按下，確認是否達門檻...", name);

    int loops = PORTAL_FORCE_HOLD_MS / PORTAL_FORCE_SAMPLE_MS;
    for (int i = 0; i < loops; i++)
    {
        if (gpio_get_level(gpio_num) != 0)
        {
            ESP_LOGI(TAG, "%s 已放開，不觸發開機功能", name);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(PORTAL_FORCE_SAMPLE_MS));
    }

    ESP_LOGI(TAG, "開機按住 %s 達門檻", name);
    return true;
}

static bool should_force_wifi_portal_on_boot(void)
{
    return is_button_held_on_boot(BUTTON_UP, "UP");
}

static bool should_clear_wifi_credentials_on_boot(void)
{
    return is_button_held_on_boot(BUTTON_DOWN, "DOWN");
}

static bool load_wifi_credentials_from_nvs(void)
{
    memset(g_wifi_ssid, 0, sizeof(g_wifi_ssid));
    memset(g_wifi_password, 0, sizeof(g_wifi_password));
    g_wifi_credentials_loaded = false;

    if (wifi_config_load_credentials(g_wifi_ssid, sizeof(g_wifi_ssid),
                                     g_wifi_password, sizeof(g_wifi_password)))
    {
        g_wifi_credentials_loaded = true;
        ESP_LOGI(TAG, "已從 NVS 載入 Wi-Fi 設定: SSID=%s", g_wifi_ssid);
        return true;
    }

    ESP_LOGW(TAG, "尚未設定 Wi-Fi credentials，將進入配網模式");
    return false;
}

static bool start_network_sync_task(bool force_unknown_during_sync)
{
    if (!g_wifi_credentials_loaded)
    {
        ESP_LOGW(TAG, "尚未載入 Wi-Fi credentials，無法啟動同步");
        return false;
    }

    if (g_time_syncing)
    {
        ESP_LOGI(TAG, "目前正在同步中");
        return false;
    }

    if (!wifi_init())
    {
        g_time_syncing = false;
        g_wifi_failed = true;
        ESP_LOGW(TAG, "WIFI 初始化失敗");
        update_ui();
        return false;
    }

    g_force_unknown_during_sync = force_unknown_during_sync;
    g_time_syncing = true;
    g_wifi_failed = false;
    update_ui();

    BaseType_t ret = xTaskCreatePinnedToCore(network_time_task,
                                             "network_time_task",
                                             8192,
                                             NULL,
                                             5,
                                             NULL,
                                             1);
    if (ret != pdPASS)
    {
        g_time_syncing = false;
        g_wifi_failed = true;
        ESP_LOGE(TAG, "建立 network_time_task 失敗");
        update_ui();
        return false;
    }

    return true;
}

/* =========================
 * Alarm
 * ========================= */
static const char *alarm_repeat_text(alarm_repeat_t repeat)
{
    return (repeat == ALARM_REPEAT_DAILY) ? "DAILY" : "ONCE";
}

static bool alarm_save_to_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ALARM_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "打開 alarm NVS 失敗: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_u8(nvs_handle, ALARM_KEY_ENABLED, g_alarm.enabled ? 1 : 0);
    if (err == ESP_OK)
        err = nvs_set_i32(nvs_handle, ALARM_KEY_HOUR, g_alarm.hour);
    if (err == ESP_OK)
        err = nvs_set_i32(nvs_handle, ALARM_KEY_MINUTE, g_alarm.minute);
    if (err == ESP_OK)
        err = nvs_set_i32(nvs_handle, ALARM_KEY_REPEAT, (int32_t)g_alarm.repeat);
    if (err == ESP_OK)
        err = nvs_commit(nvs_handle);

    nvs_close(nvs_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "儲存 alarm NVS 失敗: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "鬧鐘已儲存: enabled=%d time=%02d:%02d repeat=%s",
             g_alarm.enabled, g_alarm.hour, g_alarm.minute, alarm_repeat_text(g_alarm.repeat));
    return true;
}

static void alarm_load_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ALARM_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
    {
        if (err != ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGE(TAG, "打開 alarm NVS 失敗: %s", esp_err_to_name(err));
        }
        return;
    }

    uint8_t enabled = 0;
    int32_t hour = g_alarm.hour;
    int32_t minute = g_alarm.minute;
    int32_t repeat = (int32_t)g_alarm.repeat;

    esp_err_t e1 = nvs_get_u8(nvs_handle, ALARM_KEY_ENABLED, &enabled);
    esp_err_t e2 = nvs_get_i32(nvs_handle, ALARM_KEY_HOUR, &hour);
    esp_err_t e3 = nvs_get_i32(nvs_handle, ALARM_KEY_MINUTE, &minute);
    esp_err_t e4 = nvs_get_i32(nvs_handle, ALARM_KEY_REPEAT, &repeat);

    nvs_close(nvs_handle);

    if (e1 == ESP_OK)
        g_alarm.enabled = (enabled == 1);
    if (e2 == ESP_OK && hour >= 0 && hour <= 23)
        g_alarm.hour = (int)hour;
    if (e3 == ESP_OK && minute >= 0 && minute <= 59)
        g_alarm.minute = (int)minute;
    if (e4 == ESP_OK && (repeat == ALARM_REPEAT_ONCE || repeat == ALARM_REPEAT_DAILY))
        g_alarm.repeat = (alarm_repeat_t)repeat;

    ESP_LOGI(TAG, "載入鬧鐘: enabled=%d time=%02d:%02d repeat=%s",
             g_alarm.enabled, g_alarm.hour, g_alarm.minute, alarm_repeat_text(g_alarm.repeat));
}

static void alarm_mark_triggered(const struct tm *t)
{
    g_alarm_last_trigger_year = t->tm_year;
    g_alarm_last_trigger_yday = t->tm_yday;
    g_alarm_last_trigger_hour = t->tm_hour;
    g_alarm_last_trigger_minute = t->tm_min;
}

static bool alarm_same_as_last_trigger(const struct tm *t)
{
    return (g_alarm_last_trigger_year == t->tm_year &&
            g_alarm_last_trigger_yday == t->tm_yday &&
            g_alarm_last_trigger_hour == t->tm_hour &&
            g_alarm_last_trigger_minute == t->tm_min);
}

static void alarm_set_background_locked(lv_color_t color)
{
    lv_obj_t *scr = lv_screen_active();
    if (scr != NULL)
    {
        lv_obj_set_style_bg_color(scr, color, 0);
    }
}

static void alarm_apply_background_state_locked(void)
{
    if (g_alarm_ringing)
    {
        alarm_set_background_locked(g_alarm_flash_on ? lv_color_hex(0x707000) : lv_color_hex(0x000000));
    }
    else
    {
        alarm_set_background_locked(lv_color_hex(0x000000));
    }
}

static void alarm_sound_task(void *arg)
{
    (void)arg;

    while (g_alarm_ringing)
    {
        audio_play_beep_ms(150);
        if (!g_alarm_ringing)
            break;
        vTaskDelay(pdMS_TO_TICKS(220));

        audio_play_beep_ms(150);
        if (!g_alarm_ringing)
            break;
        vTaskDelay(pdMS_TO_TICKS(700));
    }

    g_alarm_sound_task_handle = NULL;
    vTaskDelete(NULL);
}

static void alarm_start(void)
{
    if (g_alarm_ringing)
    {
        return;
    }

    g_alarm_ringing = true;
    g_alarm_flash_on = true;
    g_alarm_last_flash_us = esp_timer_get_time();

    if (g_alarm.repeat == ALARM_REPEAT_ONCE && g_alarm.enabled)
    {
        g_alarm.enabled = false;
        alarm_save_to_nvs();
    }

    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        alarm_apply_background_state_locked();
        xSemaphoreGive(lvgl_mutex);
    }

    if (g_alarm_sound_task_handle == NULL)
    {
        xTaskCreatePinnedToCore(alarm_sound_task, "alarm_sound", 4096, NULL, 4, &g_alarm_sound_task_handle, 1);
    }

    ESP_LOGI(TAG, "鬧鐘開始響鈴");
}

static void alarm_stop(void)
{
    if (!g_alarm_ringing)
    {
        return;
    }

    g_alarm_ringing = false;
    g_alarm_flash_on = false;

    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        alarm_apply_background_state_locked();
        xSemaphoreGive(lvgl_mutex);
    }

    update_ui();
    ESP_LOGI(TAG, "鬧鐘已停止");
}

static void alarm_check_trigger(void)
{
    if (g_app_mode != APP_MODE_CLOCK)
        return;
    if (is_setting_time || g_alarm_setting_mode || g_alarm_ringing)
        return;
    if (!g_alarm.enabled)
        return;
    if (!g_time_base_valid)
        return;

    time_t now = time(NULL);
    struct tm t;
    if (localtime_r(&now, &t) == NULL)
        return;

    if (t.tm_hour == g_alarm.hour && t.tm_min == g_alarm.minute)
    {
        if (!alarm_same_as_last_trigger(&t))
        {
            alarm_mark_triggered(&t);
            alarm_start();
        }
    }
}

static void alarm_update_flash_effect(void)
{
    if (!g_alarm_ringing)
    {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    if ((now_us - g_alarm_last_flash_us) >= ALARM_FLASH_PERIOD_US)
    {
        g_alarm_last_flash_us = now_us;
        g_alarm_flash_on = !g_alarm_flash_on;

        if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(20)) == pdTRUE)
        {
            alarm_apply_background_state_locked();
            xSemaphoreGive(lvgl_mutex);
        }
    }
}

/* =========================
 * Time setting
 * ========================= */
static void enter_time_setting_mode(void)
{
    time_t now = time(NULL);

    if (localtime_r(&now, &time_setting) == NULL)
    {
        ESP_LOGW(TAG, "讀取目前系統時間失敗");
        time_setting = (struct tm){0};
    }

    is_setting_time = true;
    current_set_field = SET_FIELD_HOUR;
    current_panel = PANEL_DIGITAL;
    ESP_LOGI(TAG, "進入時間設置模式");
}

static void save_time_setting_and_exit(void)
{
    time_setting.tm_isdst = -1;

    time_t new_time = mktime(&time_setting);
    if (new_time != (time_t)-1)
    {
        struct timeval tv = {
            .tv_sec = new_time,
            .tv_usec = 0};
        settimeofday(&tv, NULL);
        rtc_save_to_nvs();

        g_time_base_valid = true;
        s_rtc_time_valid = true;

        ESP_LOGI(TAG, "時間設置已保存");
    }
    else
    {
        ESP_LOGE(TAG, "mktime 失敗，未保存時間");
    }

    is_setting_time = false;
    current_set_field = SET_FIELD_HOUR;
    current_panel = PANEL_DIGITAL;
}

static void adjust_current_field(int delta)
{
    switch (current_set_field)
    {
    case SET_FIELD_HOUR:
        time_setting.tm_hour = (time_setting.tm_hour + delta + 24) % 24;
        ESP_LOGI(TAG, "設定小時: %d", time_setting.tm_hour);
        break;

    case SET_FIELD_MINUTE:
        time_setting.tm_min = (time_setting.tm_min + delta + 60) % 60;
        ESP_LOGI(TAG, "設定分鐘: %d", time_setting.tm_min);
        break;

    case SET_FIELD_SECOND:
        time_setting.tm_sec = (time_setting.tm_sec + delta + 60) % 60;
        ESP_LOGI(TAG, "設定秒鐘: %d", time_setting.tm_sec);
        break;
    }
}

static void advance_setting_field(void)
{
    switch (current_set_field)
    {
    case SET_FIELD_HOUR:
        current_set_field = SET_FIELD_MINUTE;
        ESP_LOGI(TAG, "切換到分鐘設定");
        break;

    case SET_FIELD_MINUTE:
        current_set_field = SET_FIELD_SECOND;
        ESP_LOGI(TAG, "切換到秒鐘設定");
        break;

    case SET_FIELD_SECOND:
        current_set_field = SET_FIELD_HOUR;
        ESP_LOGI(TAG, "切換到小時設定");
        break;
    }
}

/* =========================
 * Alarm setting
 * ========================= */
static void enter_alarm_setting_mode(void)
{
    g_alarm_edit = g_alarm;
    g_alarm_setting_mode = true;
    g_alarm_set_field = ALARM_FIELD_ENABLE;
    current_panel = PANEL_ANALOG;

    ESP_LOGI(TAG, "進入鬧鐘設定模式");

    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        lv_obj_move_foreground(alarm_overlay);
        xSemaphoreGive(lvgl_mutex);
    }

    update_ui();
}

static void save_alarm_setting_and_exit(void)
{
    g_alarm = g_alarm_edit;
    g_alarm_setting_mode = false;
    alarm_save_to_nvs();
    update_ui();
}

static void adjust_alarm_field(int delta)
{
    switch (g_alarm_set_field)
    {
    case ALARM_FIELD_ENABLE:
        g_alarm_edit.enabled = !g_alarm_edit.enabled;
        ESP_LOGI(TAG, "鬧鐘啟用: %s", g_alarm_edit.enabled ? "ON" : "OFF");
        break;

    case ALARM_FIELD_REPEAT:
        g_alarm_edit.repeat = (g_alarm_edit.repeat == ALARM_REPEAT_ONCE) ? ALARM_REPEAT_DAILY : ALARM_REPEAT_ONCE;
        ESP_LOGI(TAG, "鬧鐘週期: %s", alarm_repeat_text(g_alarm_edit.repeat));
        break;

    case ALARM_FIELD_HOUR:
        g_alarm_edit.hour = (g_alarm_edit.hour + delta + 24) % 24;
        ESP_LOGI(TAG, "鬧鐘小時: %d", g_alarm_edit.hour);
        break;

    case ALARM_FIELD_MINUTE:
        g_alarm_edit.minute = (g_alarm_edit.minute + delta + 60) % 60;
        ESP_LOGI(TAG, "鬧鐘分鐘: %d", g_alarm_edit.minute);
        break;
    }

    update_ui();
}

static void advance_alarm_field(void)
{
    switch (g_alarm_set_field)
    {
    case ALARM_FIELD_ENABLE:
        g_alarm_set_field = ALARM_FIELD_REPEAT;
        break;
    case ALARM_FIELD_REPEAT:
        g_alarm_set_field = ALARM_FIELD_HOUR;
        break;
    case ALARM_FIELD_HOUR:
        g_alarm_set_field = ALARM_FIELD_MINUTE;
        break;
    case ALARM_FIELD_MINUTE:
    default:
        g_alarm_set_field = ALARM_FIELD_ENABLE;
        break;
    }

    update_ui();
}

/* =========================
 * UI helpers
 * ========================= */
static const char *weekday_name(int wday)
{
    static const char *names[] = {
        "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

    if (wday < 0 || wday > 6)
    {
        return "---";
    }
    return names[wday];
}

static const char *get_setting_status_text(void)
{
    switch (current_set_field)
    {
    case SET_FIELD_HOUR:
        return "Set Hour";
    case SET_FIELD_MINUTE:
        return "Set Minute";
    case SET_FIELD_SECOND:
        return "Set Second";
    default:
        return "Setting";
    }
}

static const char *get_top_status_text(void)
{
    if (g_app_mode == APP_MODE_WIFI_PORTAL)
    {
        return "SETUP";
    }
    else if (g_alarm_setting_mode)
    {
        return "ALARM";
    }
    else if (is_setting_time)
    {
        return "SET";
    }
    else if (g_time_syncing)
    {
        return "SYNCING";
    }
    else if (g_wifi_failed)
    {
        return "OFF";
    }
    else if (rtc_is_ntp_synced())
    {
        return "SYNCED";
    }
    else if (wifi_is_connected())
    {
        return "WIFI";
    }
    else
    {
        return "OFF";
    }
}

static void update_panel_visibility(void)
{
    if (digital_container != NULL)
    {
        if (g_app_mode == APP_MODE_CLOCK && current_panel == PANEL_DIGITAL)
        {
            lv_obj_clear_flag(digital_container, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(digital_container, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (analog_container != NULL)
    {
        if (g_app_mode == APP_MODE_CLOCK && current_panel == PANEL_ANALOG)
        {
            lv_obj_clear_flag(analog_container, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(analog_container, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (portal_container != NULL)
    {
        if (g_app_mode == APP_MODE_WIFI_PORTAL)
        {
            lv_obj_clear_flag(portal_container, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(portal_container, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void create_analog_ticks(void)
{
    if (analog_face == NULL)
    {
        return;
    }

    const int outer_r = (ANALOG_FACE_SIZE / 2) - 3;

    for (int i = 0; i < 12; i++)
    {
        int inner_r;
        int line_width;

        if (i % 3 == 0)
        {
            inner_r = outer_r - 8;
            line_width = 3;
        }
        else
        {
            inner_r = outer_r - 5;
            line_width = 2;
        }

        float rad = (float)(i * 30 - 90) * (float)M_PI / 180.0f;

        int x1 = ANALOG_CENTER_X + (int)(cosf(rad) * inner_r);
        int y1 = ANALOG_CENTER_Y + (int)(sinf(rad) * inner_r);
        int x2 = ANALOG_CENTER_X + (int)(cosf(rad) * outer_r);
        int y2 = ANALOG_CENTER_Y + (int)(sinf(rad) * outer_r);

        tick_points[i][0].x = x1;
        tick_points[i][0].y = y1;
        tick_points[i][1].x = x2;
        tick_points[i][1].y = y2;

        tick_marks[i] = lv_line_create(analog_face);
        lv_obj_set_style_line_width(tick_marks[i], line_width, 0);
        lv_obj_set_style_line_color(tick_marks[i], lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_line_rounded(tick_marks[i], true, 0);
        lv_line_set_points(tick_marks[i], tick_points[i], 2);
    }
}

static void update_hand_points(lv_obj_t *line,
                               lv_point_precise_t points[2],
                               int angle_deg,
                               int length)
{
    float rad = (float)(angle_deg - 90) * (float)M_PI / 180.0f;

    int end_x = ANALOG_CENTER_X + (int)(cosf(rad) * length);
    int end_y = ANALOG_CENTER_Y + (int)(sinf(rad) * length);

    points[0].x = ANALOG_CENTER_X;
    points[0].y = ANALOG_CENTER_Y;
    points[1].x = end_x;
    points[1].y = end_y;

    lv_line_set_points(line, points, 2);
}

static void update_analog_clock(const struct tm *t)
{
    if (hour_hand == NULL || minute_hand == NULL || second_hand == NULL)
    {
        return;
    }

    int hour_angle = (t->tm_hour % 12) * 30 + (t->tm_min / 2);
    int minute_angle = t->tm_min * 6;
    int second_angle = t->tm_sec * 6;

    update_hand_points(hour_hand, hour_points, hour_angle, HOUR_HAND_LEN);
    update_hand_points(minute_hand, minute_points, minute_angle, MINUTE_HAND_LEN);
    update_hand_points(second_hand, second_points, second_angle, SECOND_HAND_LEN);
}

static void set_field_text(lv_obj_t *label, int value, bool visible)
{
    if (label == NULL)
    {
        return;
    }

    if (visible)
    {
        lv_label_set_text_fmt(label, "%02d", value);
    }
    else
    {
        lv_label_set_text(label, "  ");
    }
}

static void set_digital_time_text(const struct tm *t)
{
    if (t == NULL)
    {
        return;
    }

    bool blink_on = true;

    if (is_setting_time)
    {
        blink_on = ((esp_timer_get_time() / SETTING_BLINK_PERIOD_US) % 2) == 0;
    }

    bool show_hour = true;
    bool show_min = true;
    bool show_sec = true;

    if (is_setting_time && !blink_on)
    {
        switch (current_set_field)
        {
        case SET_FIELD_HOUR:
            show_hour = false;
            break;
        case SET_FIELD_MINUTE:
            show_min = false;
            break;
        case SET_FIELD_SECOND:
            show_sec = false;
            break;
        }
    }

    set_field_text(hour_label, t->tm_hour, show_hour);
    set_field_text(minute_label, t->tm_min, show_min);
    set_field_text(second_label, t->tm_sec, show_sec);
}

static void set_top_info_row(lv_obj_t *status_obj,
                             lv_obj_t *date_obj,
                             lv_obj_t *weekday_obj,
                             const struct tm *t)
{
    if (t == NULL)
    {
        return;
    }

    if (status_obj != NULL)
    {
        lv_label_set_text(status_obj, get_top_status_text());
    }

    if (date_obj != NULL)
    {
        lv_label_set_text_fmt(date_obj, "%04d/%02d/%02d",
                              t->tm_year + 1900,
                              t->tm_mon + 1,
                              t->tm_mday);
    }

    if (weekday_obj != NULL)
    {
        lv_label_set_text(weekday_obj, weekday_name(t->tm_wday));
    }
}

static void set_top_info_unknown(lv_obj_t *status_obj,
                                 lv_obj_t *date_obj,
                                 lv_obj_t *weekday_obj)
{
    if (status_obj != NULL)
    {
        lv_label_set_text(status_obj, get_top_status_text());
    }

    if (date_obj != NULL)
    {
        lv_label_set_text(date_obj, "__/__/__");
    }

    if (weekday_obj != NULL)
    {
        lv_label_set_text(weekday_obj, "__");
    }
}

static void set_digital_time_unknown(void)
{
    if (hour_label != NULL)
        lv_label_set_text(hour_label, "__");
    if (minute_label != NULL)
        lv_label_set_text(minute_label, "__");
    if (second_label != NULL)
        lv_label_set_text(second_label, "__");
}

static void set_analog_clock_visible(bool visible)
{
    if (hour_hand != NULL)
    {
        if (visible)
            lv_obj_clear_flag(hour_hand, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(hour_hand, LV_OBJ_FLAG_HIDDEN);
    }

    if (minute_hand != NULL)
    {
        if (visible)
            lv_obj_clear_flag(minute_hand, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(minute_hand, LV_OBJ_FLAG_HIDDEN);
    }

    if (second_hand != NULL)
    {
        if (visible)
            lv_obj_clear_flag(second_hand, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(second_hand, LV_OBJ_FLAG_HIDDEN);
    }

    if (center_dot != NULL)
    {
        if (visible)
            lv_obj_clear_flag(center_dot, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(center_dot, LV_OBJ_FLAG_HIDDEN);
    }
}

static bool has_valid_display_time(void)
{
    if (g_app_mode == APP_MODE_WIFI_PORTAL)
    {
        return false;
    }

    if (is_setting_time)
    {
        return true;
    }

    if (g_alarm_setting_mode)
    {
        return true;
    }

    if (g_time_syncing && g_force_unknown_during_sync)
    {
        return false;
    }

    return g_time_base_valid;
}

static void set_weather_text(void)
{
    weather_info_t info;
    char buf[40];

    if (g_alarm_setting_mode)
    {
        if (weather_label != NULL)
        {
            lv_obj_set_style_text_color(weather_label, lv_color_hex(0xAAAAAA), 0);
            lv_label_set_text(weather_label, "Alarm Setting");
        }

        if (analog_weather_label != NULL)
        {
            lv_obj_set_style_text_color(analog_weather_label, lv_color_hex(0xAAAAAA), 0);
            lv_label_set_text(analog_weather_label, "Alarm Setting");
        }
        return;
    }
    else if (is_setting_time)
    {
        if (weather_label != NULL)
        {
            lv_obj_set_style_text_color(weather_label, lv_color_hex(0xAAAAAA), 0);
            lv_label_set_text(weather_label, get_setting_status_text());
        }

        if (analog_weather_label != NULL)
        {
            lv_obj_set_style_text_color(analog_weather_label, lv_color_hex(0xAAAAAA), 0);
            lv_label_set_text(analog_weather_label, get_setting_status_text());
        }
        return;
    }
    else
    {
        if (weather_label != NULL)
        {
            lv_obj_set_style_text_color(weather_label, lv_color_hex(0x00FFCC), 0);
        }

        if (analog_weather_label != NULL)
        {
            lv_obj_set_style_text_color(analog_weather_label, lv_color_hex(0x00FFCC), 0);
        }
    }

    if (g_time_syncing && g_force_unknown_during_sync)
    {
        if (weather_label != NULL)
        {
            lv_label_set_text(weather_label, "__" DEGREE_UTF8 "C  __%RH");
        }

        if (analog_weather_label != NULL)
        {
            lv_label_set_text(analog_weather_label, "__" DEGREE_UTF8 "C  __%RH");
        }

        return;
    }

    if (weather_get_info(&info) && info.valid)
    {
        snprintf(buf, sizeof(buf), "%.1f" DEGREE_UTF8 "C  %d%%RH",
                 info.temperature_c,
                 info.humidity_percent);

        if (weather_label != NULL)
        {
            lv_label_set_text(weather_label, buf);
        }

        if (analog_weather_label != NULL)
        {
            lv_label_set_text(analog_weather_label, buf);
        }
    }
    else
    {
        if (weather_label != NULL)
        {
            lv_label_set_text(weather_label, "__" DEGREE_UTF8 "C  __%RH");
        }

        if (analog_weather_label != NULL)
        {
            lv_label_set_text(analog_weather_label, "__" DEGREE_UTF8 "C  __%RH");
        }
    }
}

static void update_portal_ui(void)
{
    if (portal_title_label == NULL ||
        portal_line1_label == NULL ||
        portal_line2_label == NULL ||
        portal_line3_label == NULL ||
        portal_line4_label == NULL ||
        portal_footer_label == NULL)
    {
        return;
    }

    lv_label_set_text(portal_title_label, "WIFI SETUP");

    if (!wifi_portal_is_running())
    {
        lv_obj_set_style_text_color(portal_line1_label, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_text_color(portal_line2_label, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_text_color(portal_line3_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_color(portal_line4_label, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_text_color(portal_footer_label, lv_color_hex(0xAAAAAA), 0);

        lv_obj_set_style_text_font(portal_line1_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_font(portal_line2_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_font(portal_line3_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_font(portal_line4_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_font(portal_footer_label, &lv_font_montserrat_10, 0);

        lv_label_set_text(portal_line1_label, "");
        lv_label_set_text(portal_line2_label, "");
        lv_label_set_text(portal_line3_label, "Starting portal...");
        lv_label_set_text(portal_line4_label, "");
        lv_label_set_text(portal_footer_label, "Please wait...");
        return;
    }

    lv_obj_set_style_text_color(portal_line1_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_color(portal_line2_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(portal_line1_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_font(portal_line2_label, &lv_font_montserrat_12, 0);

    lv_label_set_text_fmt(portal_line1_label, "AP: %s", WIFI_PORTAL_DEFAULT_AP_SSID);
    lv_label_set_text_fmt(portal_line2_label, "IP: %s", wifi_portal_get_ap_ip());

    if (wifi_portal_has_new_credentials())
    {
        lv_obj_set_style_text_color(portal_line3_label, lv_color_hex(0x00FFCC), 0);
        lv_obj_set_style_text_color(portal_line4_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_color(portal_footer_label, lv_color_hex(0xAAAAAA), 0);

        lv_obj_set_style_text_font(portal_line3_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_font(portal_line4_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_font(portal_footer_label, &lv_font_montserrat_10, 0);

        lv_label_set_text_fmt(portal_line3_label, "Saved: %s", wifi_portal_get_last_ssid());
        lv_label_set_text(portal_line4_label, "Reconnecting...");
        lv_label_set_text(portal_footer_label, "Please wait...");
    }
    else
    {
        lv_obj_set_style_text_color(portal_line3_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_color(portal_line4_label, lv_color_hex(0xFF0000), 0);
        lv_obj_set_style_text_color(portal_footer_label, lv_color_hex(0xAAAAAA), 0);

        lv_obj_set_style_text_font(portal_line3_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_font(portal_line4_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_font(portal_footer_label, &lv_font_montserrat_10, 0);

        lv_label_set_text(portal_line3_label, "Connect by phone");
        lv_label_set_text_fmt(portal_line4_label, "Open http://%s", wifi_portal_get_ap_ip());
        lv_label_set_text(portal_footer_label, "Waiting for setup");
    }
}

/* =========================
 * Deep sleep / manual resync
 * ========================= */
static void enter_deep_sleep(void)
{
    if (is_setting_time || g_alarm_setting_mode)
    {
        ESP_LOGI(TAG, "設定模式中，不進入 Deep Sleep");
        return;
    }

    s_rtc_time_valid = g_time_base_valid;

    ESP_LOGI(TAG, "準備進入 Deep Sleep，等待 CENTER 放開...");

    display_prepare_for_sleep();

    while (gpio_get_level(BUTTON_CENTER) == 0)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    vTaskDelay(pdMS_TO_TICKS(50));

    if (wifi_portal_is_running())
    {
        wifi_portal_stop();
        wifi_disconnect();
    }
    else
    {
        wifi_disconnect();
    }

    rtc_gpio_pullup_en(GPIO_NUM_0);
    rtc_gpio_pulldown_dis(GPIO_NUM_0);

    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0));

    ESP_LOGI(TAG, "已進入 Deep Sleep，按下 CENTER 可喚醒");
    vTaskDelay(pdMS_TO_TICKS(20));
    esp_deep_sleep_start();
}

static void start_manual_resync(void)
{
    if (g_app_mode == APP_MODE_WIFI_PORTAL)
    {
        ESP_LOGI(TAG, "目前在配網模式中，忽略手動重同步要求");
        return;
    }

    if (is_setting_time || g_alarm_setting_mode)
    {
        ESP_LOGI(TAG, "目前在設定模式中，忽略手動重同步要求");
        return;
    }

    if (!g_wifi_credentials_loaded)
    {
        ESP_LOGW(TAG, "尚未設定 Wi-Fi，無法手動重同步");
        return;
    }

    ESP_LOGI(TAG, "手動觸發 NTP / Weather 重新同步");
    start_network_sync_task(true);
}

/* =========================
 * UI creation
 * ========================= */
static void create_boot_overlay(lv_obj_t *scr)
{
    boot_overlay = lv_obj_create(scr);
    lv_obj_set_size(boot_overlay, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_center(boot_overlay);
    lv_obj_set_style_bg_color(boot_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(boot_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(boot_overlay, 0, 0);
    lv_obj_set_style_pad_all(boot_overlay, 0, 0);
    lv_obj_clear_flag(boot_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(boot_overlay, LV_OBJ_FLAG_HIDDEN);

    /* 標題 */
    boot_overlay_title = lv_label_create(boot_overlay);
    lv_obj_set_width(boot_overlay_title, DISPLAY_WIDTH);
    lv_label_set_long_mode(boot_overlay_title, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(boot_overlay_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(boot_overlay_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(boot_overlay_title, &lv_font_montserrat_18, 0);
    lv_label_set_text(boot_overlay_title, "FORCE SETUP");
    lv_obj_set_pos(boot_overlay_title, 0, 8);

    /* 狀態提示 */
    boot_overlay_line1 = lv_label_create(boot_overlay);
    lv_obj_set_width(boot_overlay_line1, DISPLAY_WIDTH);
    lv_label_set_long_mode(boot_overlay_line1, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(boot_overlay_line1, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(boot_overlay_line1, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(boot_overlay_line1, &lv_font_montserrat_12, 0);
    lv_label_set_text(boot_overlay_line1, "Entering setup mode");
    lv_obj_set_pos(boot_overlay_line1, 0, 36);

    /* AP */
    boot_overlay_line2 = lv_label_create(boot_overlay);
    lv_obj_set_width(boot_overlay_line2, DISPLAY_WIDTH);
    lv_label_set_long_mode(boot_overlay_line2, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(boot_overlay_line2, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(boot_overlay_line2, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(boot_overlay_line2, &lv_font_montserrat_12, 0);
    lv_label_set_text(boot_overlay_line2, "AP: ClockSetup");
    lv_obj_set_pos(boot_overlay_line2, 0, 60);

    /* IP */
    boot_overlay_line3 = lv_label_create(boot_overlay);
    lv_obj_set_width(boot_overlay_line3, DISPLAY_WIDTH);
    lv_label_set_long_mode(boot_overlay_line3, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(boot_overlay_line3, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(boot_overlay_line3, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(boot_overlay_line3, &lv_font_montserrat_12, 0);
    lv_label_set_text(boot_overlay_line3, "IP: 192.168.4.1");
    lv_obj_set_pos(boot_overlay_line3, 0, 76);

    /* 底部提示 */
    boot_overlay_line4 = lv_label_create(boot_overlay);
    lv_obj_set_width(boot_overlay_line4, DISPLAY_WIDTH);
    lv_label_set_long_mode(boot_overlay_line4, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(boot_overlay_line4, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(boot_overlay_line4, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(boot_overlay_line4, &lv_font_montserrat_10, 0);
    lv_label_set_text(boot_overlay_line4, "Open on phone");
    lv_obj_set_pos(boot_overlay_line4, 0, 104);
}

static void show_boot_overlay(boot_hint_t hint)
{
    if (boot_overlay == NULL)
    {
        return;
    }

    switch (hint)
    {
    case BOOT_HINT_FORCE_SETUP:
        lv_label_set_text(boot_overlay_title, "FORCE SETUP");
        lv_label_set_text(boot_overlay_line1, "Entering setup mode");
        lv_label_set_text(boot_overlay_line2, "AP: ClockSetup");
        lv_label_set_text(boot_overlay_line3, "IP: 192.168.4.1");
        if (boot_overlay_line4 != NULL)
        {
            lv_label_set_text(boot_overlay_line4, "Open on phone");
        }
        break;

    case BOOT_HINT_CLEAR_WIFI:
        lv_label_set_text(boot_overlay_title, "CLEAR WIFI");
        lv_label_set_text(boot_overlay_line1, "Credentials erased");
        lv_label_set_text(boot_overlay_line2, "AP: ClockSetup");
        lv_label_set_text(boot_overlay_line3, "IP: 192.168.4.1");
        if (boot_overlay_line4 != NULL)
        {
            lv_label_set_text(boot_overlay_line4, "Open on phone");
        }
        break;

    case BOOT_HINT_NONE:
    default:
        lv_obj_add_flag(boot_overlay, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_move_foreground(boot_overlay);
    lv_obj_clear_flag(boot_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void hide_boot_overlay(void)
{
    if (boot_overlay != NULL)
    {
        lv_obj_add_flag(boot_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void create_alarm_overlay(lv_obj_t *scr)
{
    alarm_overlay = lv_obj_create(scr);
    lv_obj_set_size(alarm_overlay, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_center(alarm_overlay);
    lv_obj_set_style_bg_color(alarm_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(alarm_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(alarm_overlay, 0, 0);
    lv_obj_set_style_pad_all(alarm_overlay, 0, 0);
    lv_obj_clear_flag(alarm_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(alarm_overlay, LV_OBJ_FLAG_HIDDEN);

    /* 標題 */
    alarm_title_label = lv_label_create(alarm_overlay);
    lv_obj_set_width(alarm_title_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(alarm_title_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(alarm_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(alarm_title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(alarm_title_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(alarm_title_label, "ALARM SET");
    lv_obj_set_pos(alarm_title_label, 0, 6);

    /* 中間大時間：HH:MM */
    alarm_hour_label = lv_label_create(alarm_overlay);
    lv_obj_set_width(alarm_hour_label, 36);
    lv_obj_set_style_text_align(alarm_hour_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(alarm_hour_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(alarm_hour_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(alarm_hour_label, "07");
    lv_obj_set_pos(alarm_hour_label, 34, 28);

    alarm_colon_label = lv_label_create(alarm_overlay);
    lv_obj_set_width(alarm_colon_label, 16);
    lv_obj_set_style_text_align(alarm_colon_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(alarm_colon_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(alarm_colon_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(alarm_colon_label, ":");
    lv_obj_set_pos(alarm_colon_label, 72, 28);

    alarm_minute_label = lv_label_create(alarm_overlay);
    lv_obj_set_width(alarm_minute_label, 36);
    lv_obj_set_style_text_align(alarm_minute_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(alarm_minute_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(alarm_minute_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(alarm_minute_label, "00");
    lv_obj_set_pos(alarm_minute_label, 90, 28);

    /* 固定前綴：Enable: */
    lv_obj_t *alarm_enable_prefix = lv_label_create(alarm_overlay);
    lv_obj_set_style_text_color(alarm_enable_prefix, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(alarm_enable_prefix, &lv_font_montserrat_14, 0);
    lv_label_set_text(alarm_enable_prefix, "Enable:");
    lv_obj_set_pos(alarm_enable_prefix, 28, 64);

    alarm_enable_label = lv_label_create(alarm_overlay);
    lv_obj_set_width(alarm_enable_label, 48);
    lv_obj_set_style_text_align(alarm_enable_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(alarm_enable_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(alarm_enable_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(alarm_enable_label, "ON");
    lv_obj_set_pos(alarm_enable_label, 92, 64);

    /* 固定前綴：Repeat: */
    lv_obj_t *alarm_repeat_prefix = lv_label_create(alarm_overlay);
    lv_obj_set_style_text_color(alarm_repeat_prefix, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(alarm_repeat_prefix, &lv_font_montserrat_14, 0);
    lv_label_set_text(alarm_repeat_prefix, "Repeat:");
    lv_obj_set_pos(alarm_repeat_prefix, 28, 82);

    alarm_repeat_label = lv_label_create(alarm_overlay);
    lv_obj_set_width(alarm_repeat_label, 56);
    lv_obj_set_style_text_align(alarm_repeat_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(alarm_repeat_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(alarm_repeat_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(alarm_repeat_label, "DAILY");
    lv_obj_set_pos(alarm_repeat_label, 92, 82);

    /* 底部兩列提示 */
    alarm_help_label = lv_label_create(alarm_overlay);
    lv_obj_set_width(alarm_help_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(alarm_help_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(alarm_help_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(alarm_help_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(alarm_help_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(alarm_help_label, "UP/DOWN adj\nCENTER next/save");
    lv_obj_set_pos(alarm_help_label, 0, 104);
}

static void update_alarm_overlay_locked(void)
{
    if (alarm_overlay == NULL)
    {
        return;
    }

    if (!g_alarm_setting_mode)
    {
        lv_obj_add_flag(alarm_overlay, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    bool blink_on = ((esp_timer_get_time() / SETTING_BLINK_PERIOD_US) % 2) == 0;

    bool show_enable = true;
    bool show_repeat = true;
    bool show_hour = true;
    bool show_minute = true;

    if (!blink_on)
    {
        switch (g_alarm_set_field)
        {
        case ALARM_FIELD_ENABLE:
            show_enable = false;
            break;
        case ALARM_FIELD_REPEAT:
            show_repeat = false;
            break;
        case ALARM_FIELD_HOUR:
            show_hour = false;
            break;
        case ALARM_FIELD_MINUTE:
            show_minute = false;
            break;
        }
    }

    lv_label_set_text(alarm_title_label, "ALARM SET");

    if (show_hour)
        lv_label_set_text_fmt(alarm_hour_label, "%02d", g_alarm_edit.hour);
    else
        lv_label_set_text(alarm_hour_label, "  ");

    lv_label_set_text(alarm_colon_label, ":");

    if (show_minute)
        lv_label_set_text_fmt(alarm_minute_label, "%02d", g_alarm_edit.minute);
    else
        lv_label_set_text(alarm_minute_label, "  ");

    if (show_enable)
        lv_label_set_text(alarm_enable_label, g_alarm_edit.enabled ? "ON" : "OFF");
    else
        lv_label_set_text(alarm_enable_label, "   ");

    if (show_repeat)
        lv_label_set_text(alarm_repeat_label, alarm_repeat_text(g_alarm_edit.repeat));
    else
        lv_label_set_text(alarm_repeat_label, "     ");

    lv_obj_clear_flag(alarm_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(alarm_overlay);
}

static void create_digital_ui(lv_obj_t *scr)
{
    digital_container = lv_obj_create(scr);
    lv_obj_set_size(digital_container, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_center(digital_container);
    lv_obj_set_style_bg_opa(digital_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(digital_container, 0, 0);
    lv_obj_set_style_pad_all(digital_container, 0, 0);
    lv_obj_clear_flag(digital_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *top_row = lv_obj_create(digital_container);
    lv_obj_set_size(top_row, DISPLAY_WIDTH - 4, 12);
    lv_obj_align(top_row, LV_ALIGN_TOP_MID, 0, 1);
    lv_obj_set_style_bg_opa(top_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_row, 0, 0);
    lv_obj_set_style_pad_all(top_row, 0, 0);
    lv_obj_set_style_pad_column(top_row, 0, 0);
    lv_obj_clear_flag(top_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(top_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_row,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    net_status_label = lv_label_create(top_row);
    lv_obj_set_width(net_status_label, 52);
    lv_label_set_long_mode(net_status_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(net_status_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(net_status_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(net_status_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(net_status_label, "SYNCING");

    date_label = lv_label_create(top_row);
    lv_obj_set_width(date_label, 76);
    lv_label_set_long_mode(date_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(date_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(date_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(date_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(date_label, "2026/03/29");

    weekday_label = lv_label_create(top_row);
    lv_obj_set_width(weekday_label, 28);
    lv_label_set_long_mode(weekday_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(weekday_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(weekday_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(weekday_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(weekday_label, "SUN");

    lv_obj_t *time_row = lv_obj_create(digital_container);
    lv_obj_set_size(time_row,
                    DIGIT_FIELD_WIDTH * 3 + COLON_FIELD_WIDTH * 2,
                    34);
    lv_obj_align(time_row, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(time_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(time_row, 0, 0);
    lv_obj_set_style_pad_all(time_row, 0, 0);
    lv_obj_set_style_pad_column(time_row, 0, 0);
    lv_obj_clear_flag(time_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(time_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_row,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    hour_label = lv_label_create(time_row);
    lv_obj_set_width(hour_label, DIGIT_FIELD_WIDTH);
    lv_obj_set_style_text_align(hour_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(hour_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(hour_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(hour_label, "00");

    colon1_label = lv_label_create(time_row);
    lv_obj_set_width(colon1_label, COLON_FIELD_WIDTH);
    lv_obj_set_style_text_align(colon1_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(colon1_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(colon1_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(colon1_label, ":");

    minute_label = lv_label_create(time_row);
    lv_obj_set_width(minute_label, DIGIT_FIELD_WIDTH);
    lv_obj_set_style_text_align(minute_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(minute_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(minute_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(minute_label, "00");

    colon2_label = lv_label_create(time_row);
    lv_obj_set_width(colon2_label, COLON_FIELD_WIDTH);
    lv_obj_set_style_text_align(colon2_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(colon2_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(colon2_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(colon2_label, ":");

    second_label = lv_label_create(time_row);
    lv_obj_set_width(second_label, DIGIT_FIELD_WIDTH);
    lv_obj_set_style_text_align(second_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(second_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(second_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(second_label, "00");

    weather_label = lv_label_create(digital_container);
    lv_obj_set_width(weather_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(weather_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(weather_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(weather_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(weather_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(weather_label, "__" DEGREE_UTF8 "C  __%RH");
    lv_obj_align(weather_label, LV_ALIGN_BOTTOM_MID, 0, -2);
}

static void create_analog_ui(lv_obj_t *scr)
{
    analog_container = lv_obj_create(scr);
    lv_obj_set_size(analog_container, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_center(analog_container);
    lv_obj_set_style_bg_opa(analog_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(analog_container, 0, 0);
    lv_obj_set_style_pad_all(analog_container, 0, 0);
    lv_obj_clear_flag(analog_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *top_row = lv_obj_create(analog_container);
    lv_obj_set_size(top_row, DISPLAY_WIDTH - 4, 12);
    lv_obj_align(top_row, LV_ALIGN_TOP_MID, 0, 1);
    lv_obj_set_style_bg_opa(top_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_row, 0, 0);
    lv_obj_set_style_pad_all(top_row, 0, 0);
    lv_obj_set_style_pad_column(top_row, 0, 0);
    lv_obj_clear_flag(top_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(top_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_row,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    analog_status_label = lv_label_create(top_row);
    lv_obj_set_width(analog_status_label, 52);
    lv_label_set_long_mode(analog_status_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(analog_status_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(analog_status_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(analog_status_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(analog_status_label, "SYNCING");

    analog_date_label = lv_label_create(top_row);
    lv_obj_set_width(analog_date_label, 76);
    lv_label_set_long_mode(analog_date_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(analog_date_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(analog_date_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(analog_date_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(analog_date_label, "2026/03/29");

    analog_weekday_label = lv_label_create(top_row);
    lv_obj_set_width(analog_weekday_label, 28);
    lv_label_set_long_mode(analog_weekday_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(analog_weekday_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(analog_weekday_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(analog_weekday_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(analog_weekday_label, "SUN");

    analog_face = lv_obj_create(analog_container);
    lv_obj_set_size(analog_face, ANALOG_FACE_SIZE, ANALOG_FACE_SIZE);
    lv_obj_set_pos(analog_face, ANALOG_FACE_X, ANALOG_FACE_Y);
    lv_obj_set_style_radius(analog_face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(analog_face, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(analog_face, 2, 0);
    lv_obj_set_style_border_color(analog_face, lv_color_white(), 0);
    lv_obj_set_style_pad_all(analog_face, 0, 0);
    lv_obj_clear_flag(analog_face, LV_OBJ_FLAG_SCROLLABLE);

    create_analog_ticks();

    hour_hand = lv_line_create(analog_face);
    lv_obj_set_style_line_width(hour_hand, 4, 0);
    lv_obj_set_style_line_color(hour_hand, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_line_rounded(hour_hand, true, 0);

    minute_hand = lv_line_create(analog_face);
    lv_obj_set_style_line_width(minute_hand, 3, 0);
    lv_obj_set_style_line_color(minute_hand, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_line_rounded(minute_hand, true, 0);

    second_hand = lv_line_create(analog_face);
    lv_obj_set_style_line_width(second_hand, 2, 0);
    lv_obj_set_style_line_color(second_hand, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_line_rounded(second_hand, true, 0);

    center_dot = lv_obj_create(analog_face);
    lv_obj_set_size(center_dot, 7, 7);
    lv_obj_set_style_radius(center_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(center_dot, lv_color_white(), 0);
    lv_obj_set_style_border_width(center_dot, 0, 0);
    lv_obj_center(center_dot);

    /* 和 digital 底部資訊使用同一個位置 */
    analog_weather_label = lv_label_create(analog_container);
    lv_obj_set_width(analog_weather_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(analog_weather_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(analog_weather_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(analog_weather_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(analog_weather_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(analog_weather_label, "__" DEGREE_UTF8 "C  __%RH");
    lv_obj_align(analog_weather_label, LV_ALIGN_BOTTOM_MID, 0, -2);
}

static void create_portal_ui(lv_obj_t *scr)
{
    portal_container = lv_obj_create(scr);
    lv_obj_set_size(portal_container, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_center(portal_container);
    lv_obj_set_style_bg_opa(portal_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(portal_container, 0, 0);
    lv_obj_set_style_pad_all(portal_container, 0, 0);
    lv_obj_clear_flag(portal_container, LV_OBJ_FLAG_SCROLLABLE);

    /* 標題 */
    portal_title_label = lv_label_create(portal_container);
    lv_obj_set_width(portal_title_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(portal_title_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(portal_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(portal_title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(portal_title_label, &lv_font_montserrat_18, 0);
    lv_label_set_text(portal_title_label, "WIFI SETUP");
    lv_obj_set_pos(portal_title_label, 0, 6);

    /* AP */
    portal_line1_label = lv_label_create(portal_container);
    lv_obj_set_width(portal_line1_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(portal_line1_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(portal_line1_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(portal_line1_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(portal_line1_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(portal_line1_label, "AP: ClockSetup");
    lv_obj_set_pos(portal_line1_label, 0, 30);

    /* IP */
    portal_line2_label = lv_label_create(portal_container);
    lv_obj_set_width(portal_line2_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(portal_line2_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(portal_line2_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(portal_line2_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(portal_line2_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(portal_line2_label, "IP: 192.168.4.1");
    lv_obj_set_pos(portal_line2_label, 0, 46);

    /* 狀態第 1 行 */
    portal_line3_label = lv_label_create(portal_container);
    lv_obj_set_width(portal_line3_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(portal_line3_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(portal_line3_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(portal_line3_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(portal_line3_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(portal_line3_label, "Connect by phone");
    lv_obj_set_pos(portal_line3_label, 0, 70);

    /* 狀態第 2 行 */
    portal_line4_label = lv_label_create(portal_container);
    lv_obj_set_width(portal_line4_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(portal_line4_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(portal_line4_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(portal_line4_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(portal_line4_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(portal_line4_label, "Open http://192.168.4.1");
    lv_obj_set_pos(portal_line4_label, 0, 86);

    /* 底部 footer */
    portal_footer_label = lv_label_create(portal_container);
    lv_obj_set_width(portal_footer_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(portal_footer_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(portal_footer_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(portal_footer_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(portal_footer_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(portal_footer_label, "Waiting for setup");
    lv_obj_set_pos(portal_footer_label, 0, 110);
}

/* =========================
 * UI update/task
 * ========================= */
static void update_ui(void)
{
    struct tm display_time;
    bool valid_time = has_valid_display_time();

    if (valid_time)
    {
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
    }

    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        update_panel_visibility();
        alarm_apply_background_state_locked();

        if (g_app_mode == APP_MODE_WIFI_PORTAL)
        {
            update_portal_ui();
            update_alarm_overlay_locked();
            xSemaphoreGive(lvgl_mutex);
            return;
        }

        if (valid_time)
        {
            set_top_info_row(net_status_label, date_label, weekday_label, &display_time);
            set_top_info_row(analog_status_label, analog_date_label, analog_weekday_label, &display_time);
            set_digital_time_text(&display_time);

            set_analog_clock_visible(true);
            update_analog_clock(&display_time);
        }
        else
        {
            set_top_info_unknown(net_status_label, date_label, weekday_label);
            set_top_info_unknown(analog_status_label, analog_date_label, analog_weekday_label);

            set_digital_time_unknown();
            set_analog_clock_visible(false);
        }

        set_weather_text();
        update_alarm_overlay_locked();

        xSemaphoreGive(lvgl_mutex);
    }
}

static void lvgl_task(void *arg)
{
    (void)arg;

    uint32_t ui_elapsed_ms = 0;
    uint32_t alarm_check_elapsed_ms = 0;

    vTaskDelay(pdMS_TO_TICKS(300));

    while (1)
    {
        if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            lv_timer_handler();
            xSemaphoreGive(lvgl_mutex);
        }

        ui_elapsed_ms += 10;
        alarm_check_elapsed_ms += 10;

        alarm_update_flash_effect();

        uint32_t target_period = (is_setting_time || g_alarm_setting_mode) ? UI_UPDATE_PERIOD_SETTING_MS
                                                                           : UI_UPDATE_PERIOD_NORMAL_MS;

        if (ui_elapsed_ms >= target_period)
        {
            ui_elapsed_ms = 0;
            update_ui();
        }

        if (alarm_check_elapsed_ms >= 1000)
        {
            alarm_check_elapsed_ms = 0;
            alarm_check_trigger();
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void network_time_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(500));

    g_time_syncing = true;
    g_wifi_failed = false;

    ESP_LOGI(TAG, "背景開始進行 Wi-Fi / NTP / Weather 更新");

    if (wifi_connect(g_wifi_ssid, g_wifi_password))
    {
        ESP_LOGI(TAG, "WIFI 連接成功，正在同步時間...");
        rtc_sync_from_ntp();

        if (rtc_is_ntp_synced())
        {
            g_time_base_valid = true;
            s_rtc_time_valid = true;
        }

        vTaskDelay(pdMS_TO_TICKS(800));

        if (!weather_update_now())
        {
            ESP_LOGW(TAG, "天氣更新失敗");
        }
    }
    else
    {
        g_wifi_failed = true;
        ESP_LOGW(TAG, "WIFI 連接失敗 (SSID=%s)", g_wifi_ssid);

        if (s_rtc_time_valid)
        {
            g_time_base_valid = true;
        }
    }

    g_time_syncing = false;
    update_ui();

    ESP_LOGI(TAG, "背景網路更新流程結束");
    vTaskDelete(NULL);
}

/* =========================
 * Button events
 * ========================= */
void button_event_callback(uint8_t button_id, uint8_t event_type)
{
    if (g_alarm_ringing)
    {
        alarm_stop();
        return;
    }

    if (g_app_mode == APP_MODE_WIFI_PORTAL)
    {
        if (event_type == BUTTON_VERY_LONG_PRESS && button_id == BUTTON_CENTER)
        {
            g_request_deep_sleep = true;
        }
        return;
    }

    if (event_type == BUTTON_SHORT_PRESS || event_type == BUTTON_REPEAT_PRESS)
    {
        if (g_alarm_setting_mode)
        {
            switch (button_id)
            {
            case BUTTON_UP:
                adjust_alarm_field(+1);
                break;

            case BUTTON_DOWN:
                adjust_alarm_field(-1);
                break;

            case BUTTON_CENTER:
                if (event_type == BUTTON_SHORT_PRESS)
                {
                    advance_alarm_field();
                }
                break;
            }
            return;
        }

        if (is_setting_time)
        {
            switch (button_id)
            {
            case BUTTON_UP:
                adjust_current_field(+1);
                break;

            case BUTTON_DOWN:
                adjust_current_field(-1);
                break;

            case BUTTON_CENTER:
                if (event_type == BUTTON_SHORT_PRESS)
                {
                    advance_setting_field();
                }
                break;
            }
            return;
        }

        if (event_type == BUTTON_SHORT_PRESS)
        {
            switch (button_id)
            {
            case BUTTON_UP:
            case BUTTON_DOWN:
                current_panel = (current_panel == PANEL_DIGITAL) ? PANEL_ANALOG : PANEL_DIGITAL;
                ESP_LOGI(TAG, "切換錶面: %s",
                         (current_panel == PANEL_DIGITAL) ? "Digital" : "Analog");
                break;

            case BUTTON_COMBO_UP_DOWN:
                start_manual_resync();
                break;

            case BUTTON_CENTER:
                break;
            }
        }
    }
    else if (event_type == BUTTON_LONG_PRESS)
    {
        if (button_id == BUTTON_CENTER)
        {
            if (g_alarm_setting_mode)
            {
                save_alarm_setting_and_exit();
            }
            else if (!is_setting_time)
            {
                if (current_panel == PANEL_DIGITAL)
                {
                    enter_time_setting_mode();
                }
                else
                {
                    enter_alarm_setting_mode();
                }
            }
            else
            {
                save_time_setting_and_exit();
            }
        }
    }
    else if (event_type == BUTTON_VERY_LONG_PRESS)
    {
        if (button_id == BUTTON_CENTER)
        {
            g_request_deep_sleep = true;
        }
    }
}

/* =========================
 * app_main
 * ========================= */
void app_main(void)
{
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
    bool woke_from_deep_sleep = (wakeup_cause == ESP_SLEEP_WAKEUP_EXT0);
    bool has_wifi_credentials = false;
    bool force_portal = false;
    bool clear_wifi_credentials = false;

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
    ESP_LOGI(TAG, "喚醒原因: %d", (int)wakeup_cause);

    clear_wifi_credentials = should_clear_wifi_credentials_on_boot();
    force_portal = (!clear_wifi_credentials) ? should_force_wifi_portal_on_boot() : false;

    if (clear_wifi_credentials)
    {
        g_boot_hint = BOOT_HINT_CLEAR_WIFI;
        ESP_LOGI(TAG, "開機按住 DOWN：清除 Wi-Fi 設定並進入 ClockSetup");
        if (!wifi_config_clear_credentials())
        {
            ESP_LOGW(TAG, "清除 Wi-Fi 設定失敗");
        }
    }
    else if (force_portal)
    {
        g_boot_hint = BOOT_HINT_FORCE_SETUP;
    }
    else
    {
        g_boot_hint = BOOT_HINT_NONE;
    }

    has_wifi_credentials = load_wifi_credentials_from_nvs();

    if (force_portal || clear_wifi_credentials)
    {
        g_app_mode = APP_MODE_WIFI_PORTAL;
    }
    else
    {
        g_app_mode = has_wifi_credentials ? APP_MODE_CLOCK : APP_MODE_WIFI_PORTAL;
    }

    if (woke_from_deep_sleep)
    {
        display_resume_from_sleep();
    }

    ESP_LOGI(TAG, "初始化顯示模組...");
    display_init();

    lvgl_mutex = xSemaphoreCreateMutex();
    if (lvgl_mutex == NULL)
    {
        ESP_LOGE(TAG, "建立 LVGL mutex 失敗");
        return;
    }

    ESP_LOGI(TAG, "初始化按鈕模組...");
    button_init();
    button_register_callback(button_event_callback);

    ESP_LOGI(TAG, "初始化 RTC 模組...");
    my_rtc_init();

    ESP_LOGI(TAG, "初始化 Weather 模組...");
    weather_init();

    alarm_load_from_nvs();

    if (woke_from_deep_sleep && s_rtc_time_valid)
    {
        g_time_base_valid = true;
        ESP_LOGI(TAG, "從 Deep Sleep 喚醒，沿用 RTC 系統時間");
    }
    else
    {
        g_time_base_valid = false;
        ESP_LOGI(TAG, "從 NVS 載入上次的時間...");
        rtc_load_from_nvs();
    }

    const esp_timer_create_args_t tick_timer_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick"};
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_timer_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 2 * 1000));

    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        lv_obj_t *scr = lv_screen_active();
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

        create_digital_ui(scr);
        create_analog_ui(scr);
        create_portal_ui(scr);
        create_boot_overlay(scr);
        create_alarm_overlay(scr);
        update_panel_visibility();

        if (g_boot_hint != BOOT_HINT_NONE)
        {
            show_boot_overlay(g_boot_hint);
        }

        xSemaphoreGive(lvgl_mutex);
    }

    if (g_app_mode == APP_MODE_WIFI_PORTAL)
    {
        g_time_syncing = false;
        g_force_unknown_during_sync = false;
        g_wifi_failed = false;
    }
    else if (woke_from_deep_sleep && s_rtc_time_valid)
    {
        g_time_syncing = false;
        g_force_unknown_during_sync = false;
        g_wifi_failed = false;
    }
    else
    {
        g_time_syncing = false;
        g_force_unknown_during_sync = true;
        g_wifi_failed = false;
    }

    update_ui();

    ESP_LOGI(TAG, "進入主迴圈");

    if (woke_from_deep_sleep)
    {
        vTaskDelay(pdMS_TO_TICKS(80));
    }
    else
    {
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    display_wake();

    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 5, NULL, 1);

    if (g_boot_hint != BOOT_HINT_NONE)
    {
        uint32_t hint_delay_ms = (g_boot_hint == BOOT_HINT_CLEAR_WIFI)
                                     ? BOOT_HINT_CLEAR_WIFI_MS
                                     : BOOT_HINT_FORCE_SETUP_MS;

        vTaskDelay(pdMS_TO_TICKS(hint_delay_ms));

        if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            hide_boot_overlay();
            xSemaphoreGive(lvgl_mutex);
        }

        update_ui();
    }

    if (g_app_mode == APP_MODE_WIFI_PORTAL)
    {
        if (!wifi_portal_start(NULL, NULL))
        {
            ESP_LOGE(TAG, "啟動 Wi-Fi portal 失敗");
        }
        update_ui();
    }
    else
    {
        start_network_sync_task(!(woke_from_deep_sleep && s_rtc_time_valid));
    }

    while (1)
    {
        if (g_request_deep_sleep)
        {
            g_request_deep_sleep = false;
            enter_deep_sleep();
        }

        if (g_app_mode == APP_MODE_WIFI_PORTAL && wifi_portal_has_new_credentials())
        {
            ESP_LOGI(TAG, "偵測到新的 Wi-Fi credentials，準備切回 STA 模式");

            /* 先顯示：
             * Saved: <SSID>
             * Reconnecting...
             * Please wait...
             */
            update_ui();
            vTaskDelay(pdMS_TO_TICKS(PORTAL_SAVED_STATUS_MS));

            /* 顯示完成後再清旗標並切換模式 */
            wifi_portal_clear_new_credentials_flag();

            wifi_portal_stop();
            wifi_disconnect();

            if (load_wifi_credentials_from_nvs())
            {
                g_app_mode = APP_MODE_CLOCK;
                current_panel = PANEL_DIGITAL;
                g_wifi_failed = false;
                g_time_syncing = false;
                g_force_unknown_during_sync = true;

                update_ui();
                start_network_sync_task(true);
            }
            else
            {
                ESP_LOGE(TAG, "重新載入 Wi-Fi credentials 失敗，回到 portal 模式");
                g_app_mode = APP_MODE_WIFI_PORTAL;
                wifi_portal_start(NULL, NULL);
                update_ui();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}