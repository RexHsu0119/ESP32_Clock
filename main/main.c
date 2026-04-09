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

#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "esp_attr.h"

#include "driver/gpio.h"
#include "driver/rtc_io.h"

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

static bool is_setting_time = false;
static struct tm time_setting = {0};
static clock_panel_t current_panel = PANEL_DIGITAL;
static time_set_field_t current_set_field = SET_FIELD_HOUR;
static app_mode_t g_app_mode = APP_MODE_CLOCK;
static boot_hint_t g_boot_hint = BOOT_HINT_NONE;

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

/* 類比錶面：右側資訊 */
static lv_obj_t *analog_temp_label = NULL;
static lv_obj_t *analog_humidity_label = NULL;

/* 配網畫面 */
static lv_obj_t *portal_title_label = NULL;
static lv_obj_t *portal_line1_label = NULL;
static lv_obj_t *portal_line2_label = NULL;
static lv_obj_t *portal_line3_label = NULL;
static lv_obj_t *portal_line4_label = NULL;

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

/* forward declarations */
static void lvgl_task(void *arg);
static void network_time_task(void *arg);
static void update_ui(void);
static void enter_deep_sleep(void);

/* 類比時鐘尺寸 */
#define ANALOG_FACE_SIZE 64
#define ANALOG_CENTER_X (ANALOG_FACE_SIZE / 2)
#define ANALOG_CENTER_Y (ANALOG_FACE_SIZE / 2)
#define HOUR_HAND_LEN 16
#define MINUTE_HAND_LEN 22
#define SECOND_HAND_LEN 26

/* LVGL UI 更新週期 */
#define UI_UPDATE_PERIOD_NORMAL_MS 1000
#define UI_UPDATE_PERIOD_SETTING_MS 500
#define SETTING_BLINK_PERIOD_US 500000LL

/* 數位時鐘固定欄位寬度 */
#define DIGIT_FIELD_WIDTH 40
#define COLON_FIELD_WIDTH 10

/* 類比錶面方案 A：左邊時鐘，右邊溫濕度 */
#define ANALOG_FACE_X 6
#define ANALOG_FACE_Y 16
#define ANALOG_INFO_X 86
#define ANALOG_INFO_TEMP_Y 30
#define ANALOG_INFO_HUMI_Y 52
#define ANALOG_INFO_WIDTH 68

/* 開機按住 UP 強制進 ClockSetup */
/* 開機按住 DOWN 清除 Wi-Fi 設定並進 ClockSetup */
#define PORTAL_FORCE_HOLD_MS 800
#define PORTAL_FORCE_SAMPLE_MS 20

/* 開機提示畫面顯示時間 */
#define BOOT_HINT_FORCE_SETUP_MS 1000
#define BOOT_HINT_CLEAR_WIFI_MS 1200

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(2);
}

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
    {
        lv_label_set_text(hour_label, "__");
    }
    if (minute_label != NULL)
    {
        lv_label_set_text(minute_label, "__");
    }
    if (second_label != NULL)
    {
        lv_label_set_text(second_label, "__");
    }
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

    if (weather_label != NULL)
    {
        if (is_setting_time)
        {
            lv_obj_set_style_text_color(weather_label, lv_color_hex(0xAAAAAA), 0);
            lv_label_set_text(weather_label, get_setting_status_text());
            return;
        }
        else
        {
            lv_obj_set_style_text_color(weather_label, lv_color_hex(0x00FFCC), 0);
        }
    }

    if (g_time_syncing && g_force_unknown_during_sync)
    {
        if (weather_label != NULL)
        {
            lv_label_set_text(weather_label, "__" DEGREE_UTF8 "C  __%RH");
        }

        if (analog_temp_label != NULL)
        {
            lv_label_set_text(analog_temp_label, "__" DEGREE_UTF8 "C");
        }

        if (analog_humidity_label != NULL)
        {
            lv_label_set_text(analog_humidity_label, "__%RH");
        }

        return;
    }

    if (weather_get_info(&info) && info.valid)
    {
        if (weather_label != NULL)
        {
            snprintf(buf, sizeof(buf), "%.1f" DEGREE_UTF8 "C  %d%%RH",
                     info.temperature_c,
                     info.humidity_percent);
            lv_label_set_text(weather_label, buf);
        }

        if (analog_temp_label != NULL)
        {
            snprintf(buf, sizeof(buf), "%.1f" DEGREE_UTF8 "C",
                     info.temperature_c);
            lv_label_set_text(analog_temp_label, buf);
        }

        if (analog_humidity_label != NULL)
        {
            snprintf(buf, sizeof(buf), "%d%%RH",
                     info.humidity_percent);
            lv_label_set_text(analog_humidity_label, buf);
        }
    }
    else
    {
        if (weather_label != NULL)
        {
            lv_label_set_text(weather_label, "__" DEGREE_UTF8 "C  __%RH");
        }

        if (analog_temp_label != NULL)
        {
            lv_label_set_text(analog_temp_label, "__" DEGREE_UTF8 "C");
        }

        if (analog_humidity_label != NULL)
        {
            lv_label_set_text(analog_humidity_label, "__%RH");
        }
    }
}

static void update_portal_ui(void)
{
    if (portal_title_label == NULL ||
        portal_line1_label == NULL ||
        portal_line2_label == NULL ||
        portal_line3_label == NULL ||
        portal_line4_label == NULL)
    {
        return;
    }

    lv_label_set_text(portal_title_label, "WIFI SETUP");

    if (!wifi_portal_is_running())
    {
        lv_label_set_text(portal_line1_label, "Starting portal...");
        lv_label_set_text(portal_line2_label, "");
        lv_label_set_text(portal_line3_label, "");
        lv_label_set_text(portal_line4_label, "");
        return;
    }

    lv_label_set_text_fmt(portal_line1_label, "AP: %s", WIFI_PORTAL_DEFAULT_AP_SSID);
    lv_label_set_text_fmt(portal_line2_label, "IP: %s", wifi_portal_get_ap_ip());

    if (wifi_portal_has_new_credentials())
    {
        lv_obj_set_style_text_color(portal_line3_label, lv_color_hex(0x00FFCC), 0);
        lv_obj_set_style_text_color(portal_line4_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(portal_line4_label, &lv_font_montserrat_12, 0);

        lv_label_set_text_fmt(portal_line3_label, "Saved: %s", wifi_portal_get_last_ssid());
        lv_label_set_text(portal_line4_label, "Reconnecting...");
    }
    else
    {
        lv_obj_set_style_text_color(portal_line3_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_color(portal_line4_label, lv_color_hex(0xFF0000), 0);
        lv_obj_set_style_text_font(portal_line4_label, &lv_font_montserrat_10, 0);

        lv_label_set_text(portal_line3_label, "Open in phone");
        lv_label_set_text_fmt(portal_line4_label, "http://%s", wifi_portal_get_ap_ip());
    }
}

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

    boot_overlay_title = lv_label_create(boot_overlay);
    lv_obj_set_style_text_color(boot_overlay_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(boot_overlay_title, &lv_font_montserrat_18, 0);
    lv_label_set_text(boot_overlay_title, "FORCE SETUP");
    lv_obj_align(boot_overlay_title, LV_ALIGN_TOP_MID, 0, 6);

    boot_overlay_line1 = lv_label_create(boot_overlay);
    lv_obj_set_style_text_color(boot_overlay_line1, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(boot_overlay_line1, &lv_font_montserrat_12, 0);
    lv_label_set_text(boot_overlay_line1, "Entering ClockSetup");
    lv_obj_align(boot_overlay_line1, LV_ALIGN_TOP_MID, 0, 30);

    boot_overlay_line2 = lv_label_create(boot_overlay);
    lv_obj_set_style_text_color(boot_overlay_line2, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(boot_overlay_line2, &lv_font_montserrat_12, 0);
    lv_label_set_text(boot_overlay_line2, "AP: ClockSetup");
    lv_obj_align(boot_overlay_line2, LV_ALIGN_TOP_MID, 0, 46);

    boot_overlay_line3 = lv_label_create(boot_overlay);
    lv_obj_set_style_text_color(boot_overlay_line3, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(boot_overlay_line3, &lv_font_montserrat_10, 0);
    lv_label_set_text(boot_overlay_line3, "192.168.4.1");
    lv_obj_align(boot_overlay_line3, LV_ALIGN_TOP_MID, 0, 62);
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
        lv_label_set_text(boot_overlay_line1, "Entering ClockSetup");
        lv_label_set_text(boot_overlay_line2, "AP: ClockSetup");
        lv_label_set_text(boot_overlay_line3, "192.168.4.1");
        break;

    case BOOT_HINT_CLEAR_WIFI:
        lv_label_set_text(boot_overlay_title, "CLEAR WIFI");
        lv_label_set_text(boot_overlay_line1, "Credentials erased");
        lv_label_set_text(boot_overlay_line2, "Entering setup...");
        lv_label_set_text(boot_overlay_line3, "192.168.4.1");
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

static void enter_deep_sleep(void)
{
    if (is_setting_time)
    {
        ESP_LOGI(TAG, "設時模式中，不進入 Deep Sleep");
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

    if (is_setting_time)
    {
        ESP_LOGI(TAG, "目前在設時模式中，忽略手動重同步要求");
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
    lv_obj_set_size(center_dot, 6, 6);
    lv_obj_set_style_radius(center_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(center_dot, lv_color_white(), 0);
    lv_obj_set_style_border_width(center_dot, 0, 0);
    lv_obj_center(center_dot);

    analog_temp_label = lv_label_create(analog_container);
    lv_obj_set_width(analog_temp_label, ANALOG_INFO_WIDTH);
    lv_label_set_long_mode(analog_temp_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(analog_temp_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(analog_temp_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(analog_temp_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(analog_temp_label, "__" DEGREE_UTF8 "C");
    lv_obj_set_pos(analog_temp_label, ANALOG_INFO_X, ANALOG_INFO_TEMP_Y);

    analog_humidity_label = lv_label_create(analog_container);
    lv_obj_set_width(analog_humidity_label, ANALOG_INFO_WIDTH);
    lv_label_set_long_mode(analog_humidity_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(analog_humidity_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(analog_humidity_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(analog_humidity_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(analog_humidity_label, "__%RH");
    lv_obj_set_pos(analog_humidity_label, ANALOG_INFO_X, ANALOG_INFO_HUMI_Y);
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

    portal_title_label = lv_label_create(portal_container);
    lv_obj_set_width(portal_title_label, DISPLAY_WIDTH - 8);
    lv_label_set_long_mode(portal_title_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(portal_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(portal_title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(portal_title_label, &lv_font_montserrat_18, 0);
    lv_label_set_text(portal_title_label, "WIFI SETUP");
    lv_obj_set_pos(portal_title_label, 4, 4);

    portal_line1_label = lv_label_create(portal_container);
    lv_obj_set_width(portal_line1_label, DISPLAY_WIDTH - 16);
    lv_label_set_long_mode(portal_line1_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(portal_line1_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(portal_line1_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(portal_line1_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(portal_line1_label, "AP: ClockSetup");
    lv_obj_set_pos(portal_line1_label, 8, 26);

    portal_line2_label = lv_label_create(portal_container);
    lv_obj_set_width(portal_line2_label, DISPLAY_WIDTH - 16);
    lv_label_set_long_mode(portal_line2_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(portal_line2_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(portal_line2_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(portal_line2_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(portal_line2_label, "IP: 192.168.4.1");
    lv_obj_set_pos(portal_line2_label, 8, 40);

    portal_line3_label = lv_label_create(portal_container);
    lv_obj_set_width(portal_line3_label, DISPLAY_WIDTH - 16);
    lv_label_set_long_mode(portal_line3_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(portal_line3_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(portal_line3_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(portal_line3_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(portal_line3_label, "Open in phone");
    lv_obj_set_pos(portal_line3_label, 8, 56);

    portal_line4_label = lv_label_create(portal_container);
    lv_obj_set_width(portal_line4_label, DISPLAY_WIDTH - 16);
    lv_label_set_long_mode(portal_line4_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(portal_line4_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(portal_line4_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(portal_line4_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(portal_line4_label, "http://192.168.4.1");
    lv_obj_set_pos(portal_line4_label, 8, 70);
}

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

        if (g_app_mode == APP_MODE_WIFI_PORTAL)
        {
            update_portal_ui();
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

        xSemaphoreGive(lvgl_mutex);
    }
}

static void lvgl_task(void *arg)
{
    (void)arg;

    uint32_t ui_elapsed_ms = 0;

    vTaskDelay(pdMS_TO_TICKS(300));

    while (1)
    {
        if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            lv_timer_handler();
            xSemaphoreGive(lvgl_mutex);
        }

        ui_elapsed_ms += 10;

        uint32_t target_period = is_setting_time ? UI_UPDATE_PERIOD_SETTING_MS
                                                 : UI_UPDATE_PERIOD_NORMAL_MS;

        if (ui_elapsed_ms >= target_period)
        {
            ui_elapsed_ms = 0;
            update_ui();
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

void button_event_callback(uint8_t button_id, uint8_t event_type)
{
    if (g_app_mode == APP_MODE_WIFI_PORTAL)
    {
        if (event_type == BUTTON_VERY_LONG_PRESS && button_id == BUTTON_CENTER)
        {
            g_request_deep_sleep = true;
        }
        return;
    }

    if (event_type == BUTTON_SHORT_PRESS)
    {
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
                advance_setting_field();
                break;
            }
        }
        else
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
            if (!is_setting_time)
            {
                if (current_panel == PANEL_DIGITAL)
                {
                    enter_time_setting_mode();
                }
                else
                {
                    ESP_LOGI(TAG, "目前為類比時鐘畫面，不進入設時模式");
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
        /* 修正同步啟動 bug：這裡不要先設成 true */
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

            wifi_portal_clear_new_credentials_flag();
            update_ui();

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