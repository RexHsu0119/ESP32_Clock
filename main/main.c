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

#include "my_rtc.h"
#include "wifi.h"
#include "display.h"
#include "button.h"
#include "weather.h"
#include "lvgl.h"

static const char *TAG = "MAIN";

/* 建議之後改成從設定檔或 NVS 讀取，不要硬編碼在原始碼 */
static const char *WIFI_SSID = "RexHsu";
static const char *WIFI_PASSWORD = "0933356554";

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

static bool is_setting_time = false;
static struct tm time_setting = {0};
static clock_panel_t current_panel = PANEL_DIGITAL;
static time_set_field_t current_set_field = SET_FIELD_HOUR;

/* LVGL 物件 */
static lv_obj_t *digital_container = NULL;
static lv_obj_t *analog_container = NULL;

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

/* 下方天氣列 */
static lv_obj_t *weather_label = NULL;

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

/* LVGL 的時間滴答 */
static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(2);
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

static const char *get_top_status_text(void)
{
    if (is_setting_time)
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
        if (current_panel == PANEL_DIGITAL)
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
        if (current_panel == PANEL_ANALOG)
        {
            lv_obj_clear_flag(analog_container, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(analog_container, LV_OBJ_FLAG_HIDDEN);
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

static void set_top_info_text(const struct tm *t)
{
    if (t == NULL)
    {
        return;
    }

    if (net_status_label != NULL)
    {
        lv_label_set_text(net_status_label, get_top_status_text());
    }

    if (date_label != NULL)
    {
        lv_label_set_text_fmt(date_label, "%04d/%02d/%02d",
                              t->tm_year + 1900,
                              t->tm_mon + 1,
                              t->tm_mday);
    }

    if (weekday_label != NULL)
    {
        lv_label_set_text(weekday_label, weekday_name(t->tm_wday));
    }
}

static void set_weather_text(void)
{
    if (weather_label == NULL)
    {
        return;
    }

    weather_info_t info;
    if (weather_get_info(&info) && info.valid)
    {
        char buf[40];
        snprintf(buf, sizeof(buf), "%.1f" DEGREE_UTF8 "C  %d%%RH",
                 info.temperature_c,
                 info.humidity_percent);
        lv_label_set_text(weather_label, buf);
    }
    else
    {
        lv_label_set_text(weather_label, "--.-" DEGREE_UTF8 "C  --%RH");
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

static void start_manual_resync(void)
{
    if (is_setting_time)
    {
        ESP_LOGI(TAG, "目前在設時模式中，忽略手動重同步要求");
        return;
    }

    if (g_time_syncing)
    {
        ESP_LOGI(TAG, "目前正在同步中，忽略手動重同步要求");
        return;
    }

    ESP_LOGI(TAG, "手動觸發 NTP / Weather 重新同步");

    if (!wifi_init())
    {
        g_wifi_failed = true;
        ESP_LOGW(TAG, "手動重同步失敗：WIFI 初始化失敗");
        return;
    }

    g_time_syncing = true;
    g_wifi_failed = false;

    xTaskCreatePinnedToCore(network_time_task,
                            "network_time_task",
                            6144,
                            NULL,
                            5,
                            NULL,
                            1);
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

    /* 上方列：狀態 / 日期 / 星期 */
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

    /* 中間大時間 */
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

    /* 下方天氣列 */
    weather_label = lv_label_create(digital_container);
    lv_obj_set_style_text_color(weather_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(weather_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(weather_label, "--.-" DEGREE_UTF8 "C  --%RH");
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

    analog_face = lv_obj_create(analog_container);
    lv_obj_set_size(analog_face, ANALOG_FACE_SIZE, ANALOG_FACE_SIZE);
    lv_obj_align(analog_face, LV_ALIGN_CENTER, 0, -2);
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
}

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
        update_panel_visibility();

        set_top_info_text(&display_time);
        set_digital_time_text(&display_time);
        set_weather_text();
        update_analog_clock(&display_time);

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

    if (wifi_connect(WIFI_SSID, WIFI_PASSWORD))
    {
        ESP_LOGI(TAG, "WIFI 連接成功，正在同步時間...");
        rtc_sync_from_ntp();

        if (!weather_update_now())
        {
            ESP_LOGW(TAG, "天氣更新失敗");
        }
    }
    else
    {
        g_wifi_failed = true;
        ESP_LOGW(TAG, "WIFI 連接失敗，使用本地/NVS時間");
    }

    g_time_syncing = false;

    ESP_LOGI(TAG, "背景網路更新流程結束");
    vTaskDelete(NULL);
}

/* 按鈕回調函數 */
void button_event_callback(uint8_t button_id, uint8_t event_type)
{
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

    /* 初始化 Weather */
    ESP_LOGI(TAG, "初始化 Weather 模組...");
    weather_init();

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

        create_digital_ui(scr);
        create_analog_ui(scr);
        update_panel_visibility();

        xSemaphoreGive(lvgl_mutex);
    }

    /* 先顯示一次 NVS 載入後的時間 */
    g_time_syncing = true;
    g_wifi_failed = false;
    update_ui();

    ESP_LOGI(TAG, "進入主迴圈");
    display_set_brightness(100);

    /* 先初始化 WIFI */
    ESP_LOGI(TAG, "初始化 WIFI 模組...");
    bool wifi_ok = wifi_init();
    if (!wifi_ok)
    {
        g_time_syncing = false;
        g_wifi_failed = true;
        ESP_LOGW(TAG, "WIFI 初始化失敗，使用本地/NVS時間");
    }

    /* 啟動 LVGL 背景任務，放到 CPU1 */
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 5, NULL, 1);

    /* 若 WIFI 初始化成功，再啟動背景網路更新任務，放到 CPU1 */
    if (wifi_ok)
    {
        g_time_syncing = true;
        g_wifi_failed = false;
        xTaskCreatePinnedToCore(network_time_task, "network_time_task", 6144, NULL, 5, NULL, 1);
    }

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}