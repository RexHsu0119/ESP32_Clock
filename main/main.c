#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
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
#include "app_state.h"
#include "app_bootstrap.h"
#include "app_runtime.h"
#include "calendar_ui.h"
#include "clock_types.h"
#include "calendar_logic.h"
#include "alarm_logic.h"
#include "alarm_runtime.h"
#include "input_handler.h"
#include "menu_logic.h"
#include "network_sync.h"
#include "settings_logic.h"
#include "timer_logic.h"
#include "ui.h"
#include "ui_clock.h"
#include "lvgl.h"

static const char *TAG = "MAIN";

#define DEGREE_UTF8 "\xC2\xB0"

/* LVGL UI 更新週期 */
#define UI_UPDATE_PERIOD_NORMAL_MS 1000
#define UI_UPDATE_PERIOD_SETTING_MS 500
#define SETTING_BLINK_PERIOD_US 500000LL
#define ALARM_FLASH_PERIOD_US 400000LL

/* 開機按住 UP 強制進 ClockSetup */
/* 開機按住 DOWN 清除 Wi-Fi 設定並進 ClockSetup */
#define PORTAL_FORCE_HOLD_MS 800
#define PORTAL_FORCE_SAMPLE_MS 20

/* 開機提示畫面顯示時間 */
#define BOOT_HINT_FORCE_SETUP_MS 2500
#define BOOT_HINT_CLEAR_WIFI_MS 2500

/* Wi-Fi portal 成功儲存後，保留提示畫面的顯示時間 */
#define PORTAL_SAVED_STATUS_MS 1500

/* forward declarations */
static void update_ui(void);
static void update_panel_visibility(void);
static void lvgl_task(void *arg);
static void lvgl_tick_cb(void *arg);
static void enter_deep_sleep(void);
static void menu_open(void);
static void menu_close(void);
static void menu_move(int delta);
static void menu_execute_selected(void);
static void start_manual_resync(void);
static void enter_timer_mode(void);
static void enter_time_setting_mode(void);
static void enter_alarm_setting_mode(void);
static void exit_alarm_setting_mode(void);
static void move_alarm_selection(int delta);
static void toggle_selected_alarm_enabled(void);
static void enter_selected_alarm_edit(void);
static void cancel_alarm_edit(void);

void button_event_callback(uint8_t button_id, uint8_t event_type);

static void confirm_close(void);
static void confirm_execute(void);

static input_handler_state_t build_input_handler_state(void);

static void ih_stop_alarm(void);
static void ih_request_deep_sleep(void);

static void ih_confirm_select_no(void);
static void ih_confirm_select_yes(void);
static void ih_confirm_execute(void);
static void ih_confirm_close(void);

static void ih_menu_move(int delta);
static void ih_menu_execute(void);
static void ih_menu_close(void);
static void ih_menu_open(void);

static void ih_timer_adjust(int delta);
static void ih_timer_advance_field(void);
static void ih_timer_clear(void);
static void ih_timer_start_or_exit(void);
static void ih_timer_pause_resume(void);
static void ih_timer_cancel(void);
static void ih_timer_exit(void);
static void ih_timer_ack_done(void);
static void start_timer_done_alert(void);
static void stop_timer_done_alert(void);
static void ih_alarm_move_selection(int delta);
static void ih_alarm_toggle_selected_enabled(void);
static void ih_alarm_enter_selected_edit(void);
static void ih_alarm_cancel_edit(void);
static void ih_alarm_exit_setting(void);
static void ih_adjust_alarm(int delta);
static void ih_advance_alarm_field(void);
static void ih_save_alarm_setting_and_exit(void);

static void ih_adjust_time(int delta);
static void ih_advance_time_field(void);
static void ih_save_time_setting_and_exit(void);

static void ih_enter_time_setting_mode(void);
static void ih_enter_alarm_setting_mode(void);

static void ih_calendar_toggle_adjust_field(void);
static void ih_calendar_change_year(int delta);
static void ih_calendar_change_month(int delta);
static void ih_calendar_reset_to_current_month(void);
static void ih_calendar_return_to_previous_clock(void);

static void ih_set_panel_digital(void);
static void ih_set_panel_analog(void);

static network_sync_context_t g_network_sync = {
    .log_tag = "MAIN",
    .wifi_credentials_loaded = &g_app.wifi_credentials_loaded,
    .wifi_ssid = g_app.wifi_ssid,
    .wifi_ssid_size = sizeof(g_app.wifi_ssid),
    .wifi_password = g_app.wifi_password,
    .wifi_password_size = sizeof(g_app.wifi_password),
    .time_syncing = &g_app.time_syncing,
    .wifi_failed = &g_app.wifi_failed,
    .force_unknown_during_sync = &g_app.force_unknown_during_sync,
    .time_base_valid = &g_app.time_base_valid,
    .rtc_time_valid = &g_rtc_time_valid,
};

static settings_logic_context_t g_settings_logic = {
    .log_tag = "MAIN",
    .is_setting_time = &g_app.is_setting_time,
    .time_setting = &g_app.time_setting,
    .current_set_field = &g_app.current_set_field,
    .current_panel = &g_app.current_panel,
    .last_clock_panel_before_overlay = &g_app.last_clock_panel_before_overlay,
    .time_base_valid = &g_app.time_base_valid,
    .rtc_time_valid = &g_rtc_time_valid,
    .alarms = g_app.alarms,
    .alarm_count = ALARM_SLOT_COUNT,
    .alarm_edit = &g_app.alarm_edit,
    .alarm_setting_mode = &g_app.alarm_setting_mode,
    .alarm_ui_mode = &g_app.alarm_ui_mode,
    .selected_alarm_index = &g_app.selected_alarm_index,
    .alarm_set_field = &g_app.alarm_set_field,
};

static timer_logic_context_t g_timer_logic = {
    .log_tag = "MAIN",
    .timer_mode = &g_app.timer_mode,
    .timer_state = &g_app.timer_state,
    .timer_set_field = &g_app.timer_set_field,
    .timer_hours = &g_app.timer_hours,
    .timer_minutes = &g_app.timer_minutes,
    .timer_seconds = &g_app.timer_seconds,
    .timer_remaining_seconds = &g_app.timer_remaining_seconds,
    .timer_end_time_us = &g_app.timer_end_time_us,
    .current_panel = &g_app.current_panel,
    .last_clock_panel_before_overlay = &g_app.last_clock_panel_before_overlay,
    .start_done_alert = start_timer_done_alert,
    .stop_done_alert = stop_timer_done_alert,
};

static menu_logic_context_t g_menu_logic = {
    .menu_open = &g_app.menu_open,
    .menu_selected = &g_app.menu_selected,
    .menu_top_index = &g_app.menu_top_index,
    .confirm_open = &g_app.confirm_open,
    .confirm_action = &g_app.confirm_action,
    .confirm_yes_selected = &g_app.confirm_yes_selected,
    .request_deep_sleep = &g_app.request_deep_sleep,
    .request_open_wifi_setup = &g_app.request_open_wifi_setup,
    .request_clear_wifi = &g_app.request_clear_wifi,
    .current_panel = &g_app.current_panel,
    .last_clock_panel_before_overlay = &g_app.last_clock_panel_before_overlay,
    .calendar_adjust_field = &g_app.calendar_adjust_field,
    .calendar_year = &g_app.calendar_year,
    .calendar_month = &g_app.calendar_month,
    .enter_timer_mode = enter_timer_mode,
    .enter_time_setting_mode = enter_time_setting_mode,
    .enter_alarm_setting_mode = enter_alarm_setting_mode,
    .start_manual_resync = start_manual_resync,
};

static app_bootstrap_context_t g_app_bootstrap = {
    .log_tag = "MAIN",
    .network_sync = &g_network_sync,
    .alarms = g_app.alarms,
    .app_mode = &g_app.app_mode,
    .boot_hint = &g_app.boot_hint,
    .time_base_valid = &g_app.time_base_valid,
    .rtc_time_valid = &g_rtc_time_valid,
    .force_unknown_during_sync = &g_app.force_unknown_during_sync,
    .time_syncing = &g_app.time_syncing,
    .wifi_failed = &g_app.wifi_failed,
    .calendar_year = &g_app.calendar_year,
    .calendar_month = &g_app.calendar_month,
    .lvgl_mutex = &g_app.lvgl_mutex,
    .portal_force_hold_ms = PORTAL_FORCE_HOLD_MS,
    .portal_force_sample_ms = PORTAL_FORCE_SAMPLE_MS,
    .boot_hint_force_setup_ms = BOOT_HINT_FORCE_SETUP_MS,
    .boot_hint_clear_wifi_ms = BOOT_HINT_CLEAR_WIFI_MS,
    .update_ui = update_ui,
    .button_callback = button_event_callback,
    .lvgl_tick_cb = lvgl_tick_cb,
    .lvgl_task = lvgl_task,
    .create_calendar_ui = calendar_ui_create,
    .update_panel_visibility = update_panel_visibility,
};

static app_runtime_context_t g_app_runtime = {
    .log_tag = "MAIN",
    .network_sync = &g_network_sync,
    .request_deep_sleep = &g_app.request_deep_sleep,
    .request_open_wifi_setup = &g_app.request_open_wifi_setup,
    .request_clear_wifi = &g_app.request_clear_wifi,
    .wifi_ssid = g_app.wifi_ssid,
    .wifi_ssid_size = sizeof(g_app.wifi_ssid),
    .wifi_password = g_app.wifi_password,
    .wifi_password_size = sizeof(g_app.wifi_password),
    .wifi_credentials_loaded = &g_app.wifi_credentials_loaded,
    .menu_open = &g_app.menu_open,
    .confirm_open = &g_app.confirm_open,
    .confirm_action = &g_app.confirm_action,
    .confirm_yes_selected = &g_app.confirm_yes_selected,
    .app_mode = &g_app.app_mode,
    .current_panel = &g_app.current_panel,
    .wifi_failed = &g_app.wifi_failed,
    .time_syncing = &g_app.time_syncing,
    .force_unknown_during_sync = &g_app.force_unknown_during_sync,
    .portal_saved_status_ms = PORTAL_SAVED_STATUS_MS,
    .enter_deep_sleep = enter_deep_sleep,
};

static alarm_runtime_context_t g_alarm_runtime = {
    .log_tag = "MAIN",
    .app = &g_app,
    .flash_period_us = ALARM_FLASH_PERIOD_US,
};

#define is_setting_time g_app.is_setting_time
#define time_setting g_app.time_setting
#define current_panel g_app.current_panel
#define current_set_field g_app.current_set_field
#define g_app_mode g_app.app_mode
#define g_boot_hint g_app.boot_hint
#define g_alarms g_app.alarms
#define g_alarm_edit g_app.alarm_edit
#define g_alarm_setting_mode g_app.alarm_setting_mode
#define g_alarm_ui_mode g_app.alarm_ui_mode
#define g_selected_alarm_index g_app.selected_alarm_index
#define g_alarm_set_field g_app.alarm_set_field
#define g_alarm_ringing g_app.alarm_ringing
#define g_alarm_flash_on g_app.alarm_flash_on
#define g_alarm_last_flash_us g_app.alarm_last_flash_us
#define g_alarm_sound_task_handle g_app.alarm_sound_task_handle
#define g_timer_mode g_app.timer_mode
#define g_timer_state g_app.timer_state
#define g_timer_set_field g_app.timer_set_field
#define g_timer_hours g_app.timer_hours
#define g_timer_minutes g_app.timer_minutes
#define g_timer_seconds g_app.timer_seconds
#define g_timer_remaining_seconds g_app.timer_remaining_seconds
#define g_timer_alert_active g_app.timer_alert_active
#define g_time_base_valid g_app.time_base_valid
#define g_force_unknown_during_sync g_app.force_unknown_during_sync
#define s_rtc_time_valid g_rtc_time_valid
#define g_request_deep_sleep g_app.request_deep_sleep
#define g_wifi_ssid g_app.wifi_ssid
#define g_wifi_password g_app.wifi_password
#define g_wifi_credentials_loaded g_app.wifi_credentials_loaded
#define g_menu_open g_app.menu_open
#define g_menu_selected g_app.menu_selected
#define g_menu_top_index g_app.menu_top_index
#define g_request_open_wifi_setup g_app.request_open_wifi_setup
#define g_request_clear_wifi g_app.request_clear_wifi
#define g_last_clock_panel_before_overlay g_app.last_clock_panel_before_overlay
#define g_confirm_open g_app.confirm_open
#define g_confirm_action g_app.confirm_action
#define g_confirm_yes_selected g_app.confirm_yes_selected
#define g_calendar_adjust_field g_app.calendar_adjust_field
#define g_calendar_year g_app.calendar_year
#define g_calendar_month g_app.calendar_month
#define lvgl_mutex g_app.lvgl_mutex
#define g_time_syncing g_app.time_syncing
#define g_wifi_failed g_app.wifi_failed

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(2);
}

/* =========================
 * Timer
 * ========================= */
static void enter_timer_mode(void)
{
    timer_logic_enter_mode(&g_timer_logic);
}

static void adjust_timer_field(int delta)
{
    timer_logic_adjust_field(&g_timer_logic, delta);
}

static void advance_timer_field(void)
{
    timer_logic_advance_field(&g_timer_logic);
}

static void clear_timer(void)
{
    timer_logic_clear(&g_timer_logic);
}

static void start_timer_or_exit(void)
{
    timer_logic_start_or_exit(&g_timer_logic);
}

static void pause_resume_timer(void)
{
    timer_logic_pause_resume(&g_timer_logic);
}

static void cancel_timer(void)
{
    timer_logic_cancel(&g_timer_logic);
}

static void exit_timer_mode(void)
{
    timer_logic_exit_mode(&g_timer_logic);
}

static void ack_timer_done(void)
{
    timer_logic_ack_done(&g_timer_logic);
}

static void start_timer_done_alert(void)
{
    alarm_runtime_start_timer_alert(&g_alarm_runtime);
}

static void stop_timer_done_alert(void)
{
    alarm_runtime_stop_timer_alert(&g_alarm_runtime);
}

/* =========================
 * Time setting
 * ========================= */
static void enter_time_setting_mode(void)
{
    settings_logic_enter_time_setting_mode(&g_settings_logic);
}

static void save_time_setting_and_exit(void)
{
    settings_logic_save_time_setting_and_exit(&g_settings_logic);
}

static void adjust_current_field(int delta)
{
    settings_logic_adjust_current_field(&g_settings_logic, delta);
}

static void advance_setting_field(void)
{
    settings_logic_advance_setting_field(&g_settings_logic);
}

/* =========================
 * Alarm setting
 * ========================= */
static void enter_alarm_setting_mode(void)
{
    settings_logic_enter_alarm_setting_mode(&g_settings_logic);
}

static void exit_alarm_setting_mode(void)
{
    settings_logic_exit_alarm_setting_mode(&g_settings_logic);
}

static void move_alarm_selection(int delta)
{
    settings_logic_move_alarm_selection(&g_settings_logic, delta);
}

static void toggle_selected_alarm_enabled(void)
{
    settings_logic_toggle_selected_alarm_enabled(&g_settings_logic);
}

static void enter_selected_alarm_edit(void)
{
    settings_logic_enter_selected_alarm_edit(&g_settings_logic);
}

static void cancel_alarm_edit(void)
{
    settings_logic_cancel_alarm_edit(&g_settings_logic);
}

static void save_alarm_setting_and_exit(void)
{
    settings_logic_save_alarm_setting_and_exit(&g_settings_logic);
}

static void adjust_alarm_field(int delta)
{
    settings_logic_adjust_alarm_field(&g_settings_logic, delta);
}

static void advance_alarm_field(void)
{
    settings_logic_advance_alarm_field(&g_settings_logic);
}

/* =========================
 * Menu helpers
 * ========================= */
static const char *menu_item_text(menu_item_t item)
{
    return menu_logic_item_text(item);
}

static const char *ui_menu_item_text_cb(int item)
{
    return menu_item_text((menu_item_t)item);
}

static void menu_open(void)
{
    menu_logic_open(&g_menu_logic);
}

static void menu_close(void)
{
    menu_logic_close(&g_menu_logic);
}

static void menu_move(int delta)
{
    menu_logic_move(&g_menu_logic, delta);
}

static void confirm_close(void)
{
    menu_logic_confirm_close(&g_menu_logic);
}

static void confirm_execute(void)
{
    menu_logic_confirm_execute(&g_menu_logic);
}

static void menu_execute_selected(void)
{
    menu_logic_execute_selected(&g_menu_logic);
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
    ui_clock_set_digital_panel_visible(g_app_mode == APP_MODE_CLOCK && current_panel == PANEL_DIGITAL);
    ui_clock_set_analog_panel_visible(g_app_mode == APP_MODE_CLOCK && current_panel == PANEL_ANALOG);

    calendar_ui_set_visible(g_app_mode == APP_MODE_CLOCK && current_panel == PANEL_CALENDAR);

    ui_set_portal_visible(g_app_mode == APP_MODE_WIFI_PORTAL);
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
    const char *text = NULL;
    lv_color_t color = lv_color_hex(0x00FFCC);

    if (g_alarm_setting_mode)
    {
        text = "Alarm Setting";
        color = lv_color_hex(0xAAAAAA);
        ui_clock_set_weather_text(text, color);
        return;
    }
    else if (is_setting_time)
    {
        text = get_setting_status_text();
        color = lv_color_hex(0xAAAAAA);
        ui_clock_set_weather_text(text, color);
        return;
    }

    if (g_time_syncing && g_force_unknown_during_sync)
    {
        text = "__" DEGREE_UTF8 "C  __%RH";
        ui_clock_set_weather_text(text, color);
        return;
    }

    if (weather_get_info(&info) && info.valid)
    {
        snprintf(buf, sizeof(buf), "%.1f" DEGREE_UTF8 "C  %d%%RH",
                 info.temperature_c,
                 info.humidity_percent);
        text = buf;
    }
    else
    {
        text = "__" DEGREE_UTF8 "C  __%RH";
    }

    ui_clock_set_weather_text(text, color);
}

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
    network_sync_start(&g_network_sync, true);
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

    if (ui_lock(pdMS_TO_TICKS(50)))
    {
        update_panel_visibility();
        alarm_runtime_apply_background_state_locked(&g_alarm_runtime);

        if (g_app_mode == APP_MODE_WIFI_PORTAL)
        {
            ui_update_portal_ui();

            ui_update_alarm_overlay_locked(g_alarm_setting_mode,
                                           (int)g_alarm_ui_mode,
                                           g_selected_alarm_index,
                                           g_alarms,
                                           ALARM_SLOT_COUNT,
                                           (int)g_alarm_set_field,
                                           g_alarm_edit.enabled,
                                           (int)g_alarm_edit.repeat,
                                           g_alarm_edit.hour,
                                           g_alarm_edit.minute);

            ui_update_timer_overlay_locked(g_timer_mode,
                                           (int)g_timer_state,
                                           (int)g_timer_set_field,
                                           g_timer_hours,
                                           g_timer_minutes,
                                           g_timer_seconds,
                                           g_timer_remaining_seconds);

            ui_update_menu_overlay_locked(g_menu_open,
                                          (g_app_mode == APP_MODE_CLOCK),
                                          (int)g_menu_selected,
                                          g_menu_top_index,
                                          MENU_ITEM_COUNT,
                                          ui_menu_item_text_cb);

            ui_update_confirm_overlay_locked(g_confirm_open,
                                             (g_app_mode == APP_MODE_CLOCK),
                                             (int)g_confirm_action,
                                             g_confirm_yes_selected);

            ui_unlock();
            return;
        }

        if (valid_time)
        {
            bool show_hour = true;
            bool show_min = true;
            bool show_sec = true;

            if (is_setting_time)
            {
                bool blink_on = ((esp_timer_get_time() / SETTING_BLINK_PERIOD_US) % 2) == 0;
                if (!blink_on)
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
            }

            ui_clock_set_digital_top_info(get_top_status_text(),
                                          display_time.tm_year + 1900,
                                          display_time.tm_mon + 1,
                                          display_time.tm_mday,
                                          weekday_name(display_time.tm_wday));

            ui_clock_set_analog_top_info(get_top_status_text(),
                                         display_time.tm_year + 1900,
                                         display_time.tm_mon + 1,
                                         display_time.tm_mday,
                                         weekday_name(display_time.tm_wday));

            ui_clock_set_digital_time(display_time.tm_hour,
                                      display_time.tm_min,
                                      display_time.tm_sec,
                                      show_hour,
                                      show_min,
                                      show_sec);

            ui_clock_set_analog_visible(true);
            ui_clock_set_analog_time(display_time.tm_hour,
                                     display_time.tm_min,
                                     display_time.tm_sec);
        }
        else
        {
            ui_clock_set_digital_top_info_unknown(get_top_status_text());
            ui_clock_set_analog_top_info_unknown(get_top_status_text());

            ui_clock_set_digital_time_unknown();
            ui_clock_set_analog_visible(false);
        }

        set_weather_text();
        calendar_ui_update(valid_time,
                           g_calendar_adjust_field,
                           &g_calendar_year,
                           &g_calendar_month);

        ui_update_alarm_overlay_locked(g_alarm_setting_mode,
                                       (int)g_alarm_ui_mode,
                                       g_selected_alarm_index,
                                       g_alarms,
                                       ALARM_SLOT_COUNT,
                                       (int)g_alarm_set_field,
                                       g_alarm_edit.enabled,
                                       (int)g_alarm_edit.repeat,
                                       g_alarm_edit.hour,
                                       g_alarm_edit.minute);

        ui_update_timer_overlay_locked(g_timer_mode,
                                       (int)g_timer_state,
                                       (int)g_timer_set_field,
                                       g_timer_hours,
                                       g_timer_minutes,
                                       g_timer_seconds,
                                       g_timer_remaining_seconds);

        ui_update_menu_overlay_locked(g_menu_open,
                                      (g_app_mode == APP_MODE_CLOCK),
                                      (int)g_menu_selected,
                                      g_menu_top_index,
                                      MENU_ITEM_COUNT,
                                      ui_menu_item_text_cb);

        ui_update_confirm_overlay_locked(g_confirm_open,
                                         (g_app_mode == APP_MODE_CLOCK),
                                         (int)g_confirm_action,
                                         g_confirm_yes_selected);

        ui_unlock();
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
        if (ui_lock(pdMS_TO_TICKS(50)))
        {
            lv_timer_handler();
            ui_unlock();
        }

        ui_elapsed_ms += 10;
        alarm_check_elapsed_ms += 10;

        alarm_runtime_update_flash_effect(&g_alarm_runtime);
        timer_logic_update(&g_timer_logic);

        uint32_t target_period = (is_setting_time || g_alarm_setting_mode ||
                                  (g_timer_mode && (g_timer_state == TIMER_STATE_SET || g_timer_state == TIMER_STATE_DONE)))
                                     ? UI_UPDATE_PERIOD_SETTING_MS
                                     : UI_UPDATE_PERIOD_NORMAL_MS;

        if (ui_elapsed_ms >= target_period)
        {
            ui_elapsed_ms = 0;
            ui_refresh();
        }

        if (alarm_check_elapsed_ms >= 1000)
        {
            alarm_check_elapsed_ms = 0;
            alarm_runtime_check_trigger(&g_alarm_runtime);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static input_handler_state_t build_input_handler_state(void)
{
    input_handler_state_t s = {
        .alarm_ringing = g_alarm_ringing,
        .wifi_portal_mode = (g_app_mode == APP_MODE_WIFI_PORTAL),
        .confirm_open = g_confirm_open,
        .menu_open = g_menu_open,
        .alarm_setting_mode = g_alarm_setting_mode,
        .alarm_list_mode = (g_alarm_ui_mode == ALARM_UI_LIST),
        .alarm_edit_mode = (g_alarm_ui_mode == ALARM_UI_EDIT),
        .timer_mode = g_timer_mode,
        .timer_set_mode = (g_timer_state == TIMER_STATE_SET || g_timer_state == TIMER_STATE_IDLE),
        .timer_running = (g_timer_state == TIMER_STATE_RUNNING),
        .timer_paused = (g_timer_state == TIMER_STATE_PAUSED),
        .timer_done = (g_timer_state == TIMER_STATE_DONE),
        .time_setting_mode = is_setting_time,
        .calendar_active = (current_panel == PANEL_CALENDAR),
        .calendar_adjust_year_selected = (g_calendar_adjust_field == CALENDAR_ADJUST_YEAR),
        .digital_active = (current_panel == PANEL_DIGITAL),
        .analog_active = (current_panel == PANEL_ANALOG),
    };
    return s;
}

static void ih_stop_alarm(void)
{
    alarm_runtime_stop(&g_alarm_runtime);
}

static void ih_request_deep_sleep(void)
{
    g_request_deep_sleep = true;
}

static void ih_confirm_select_no(void)
{
    g_confirm_yes_selected = false;
    ui_refresh();
}

static void ih_confirm_select_yes(void)
{
    g_confirm_yes_selected = true;
    ui_refresh();
}

static void ih_confirm_execute(void)
{
    confirm_execute();
}

static void ih_confirm_close(void)
{
    confirm_close();
}

static void ih_menu_move(int delta)
{
    menu_move(delta);
}

static void ih_menu_execute(void)
{
    menu_execute_selected();
}

static void ih_menu_close(void)
{
    menu_close();
}

static void ih_menu_open(void)
{
    menu_open();
}

static void ih_timer_adjust(int delta)
{
    adjust_timer_field(delta);
}

static void ih_timer_advance_field(void)
{
    advance_timer_field();
}

static void ih_timer_clear(void)
{
    clear_timer();
}

static void ih_timer_start_or_exit(void)
{
    start_timer_or_exit();
}

static void ih_timer_pause_resume(void)
{
    pause_resume_timer();
}

static void ih_timer_cancel(void)
{
    cancel_timer();
}

static void ih_timer_exit(void)
{
    exit_timer_mode();
}

static void ih_timer_ack_done(void)
{
    ack_timer_done();
}

static void ih_alarm_move_selection(int delta)
{
    move_alarm_selection(delta);
    ui_refresh();
}

static void ih_alarm_toggle_selected_enabled(void)
{
    toggle_selected_alarm_enabled();
    ui_refresh();
}

static void ih_alarm_enter_selected_edit(void)
{
    enter_selected_alarm_edit();
    ui_refresh();
}

static void ih_alarm_cancel_edit(void)
{
    cancel_alarm_edit();
    ui_refresh();
}

static void ih_alarm_exit_setting(void)
{
    exit_alarm_setting_mode();
    ui_refresh();
}

static void ih_adjust_alarm(int delta)
{
    adjust_alarm_field(delta);
}

static void ih_advance_alarm_field(void)
{
    advance_alarm_field();
}

static void ih_save_alarm_setting_and_exit(void)
{
    save_alarm_setting_and_exit();
}

static void ih_adjust_time(int delta)
{
    adjust_current_field(delta);
}

static void ih_advance_time_field(void)
{
    advance_setting_field();
}

static void ih_save_time_setting_and_exit(void)
{
    save_time_setting_and_exit();
    ui_refresh();
}

static void ih_enter_time_setting_mode(void)
{
    enter_time_setting_mode();
    ui_refresh();
}

static void ih_enter_alarm_setting_mode(void)
{
    enter_alarm_setting_mode();
    ui_refresh();
}

static void ih_enter_timer_mode(void)
{
    enter_timer_mode();
    ui_refresh();
}

static void ih_calendar_toggle_adjust_field(void)
{
    g_calendar_adjust_field = (g_calendar_adjust_field == CALENDAR_ADJUST_YEAR)
                                  ? CALENDAR_ADJUST_MONTH
                                  : CALENDAR_ADJUST_YEAR;
    ui_refresh();
}

static void ih_calendar_change_year(int delta)
{
    g_calendar_adjust_field = CALENDAR_ADJUST_YEAR;
    calendar_change_year(&g_calendar_year, &g_calendar_month, delta);
    ui_refresh();
}

static void ih_calendar_change_month(int delta)
{
    g_calendar_adjust_field = CALENDAR_ADJUST_MONTH;
    calendar_change_month(&g_calendar_year, &g_calendar_month, delta);
    ui_refresh();
}

static void ih_calendar_reset_to_current_month(void)
{
    g_calendar_adjust_field = CALENDAR_ADJUST_MONTH;
    calendar_reset_to_current_month(&g_calendar_year, &g_calendar_month);
    ESP_LOGI(TAG, "月曆回到本月");
    ui_refresh();
}

static void ih_calendar_return_to_previous_clock(void)
{
    current_panel = g_last_clock_panel_before_overlay;
    ui_refresh();
}

static void ih_set_panel_digital(void)
{
    current_panel = PANEL_DIGITAL;
    ui_refresh();
}

static void ih_set_panel_analog(void)
{
    current_panel = PANEL_ANALOG;
    ui_refresh();
}

/* =========================
 * Button events
 * ========================= */
void button_event_callback(uint8_t button_id, uint8_t event_type)
{
    static const input_handler_ops_t ops = {
        .stop_alarm = ih_stop_alarm,
        .request_deep_sleep = ih_request_deep_sleep,

        .confirm_select_no = ih_confirm_select_no,
        .confirm_select_yes = ih_confirm_select_yes,
        .confirm_execute = ih_confirm_execute,
        .confirm_close = ih_confirm_close,

        .menu_move = ih_menu_move,
        .menu_execute = ih_menu_execute,
        .menu_close = ih_menu_close,
        .menu_open = ih_menu_open,

        .timer_adjust = ih_timer_adjust,
        .timer_advance_field = ih_timer_advance_field,
        .timer_clear = ih_timer_clear,
        .timer_start_or_exit = ih_timer_start_or_exit,
        .timer_pause_resume = ih_timer_pause_resume,
        .timer_cancel = ih_timer_cancel,
        .timer_exit = ih_timer_exit,
        .timer_ack_done = ih_timer_ack_done,

        .alarm_move_selection = ih_alarm_move_selection,
        .alarm_toggle_selected_enabled = ih_alarm_toggle_selected_enabled,
        .alarm_enter_selected_edit = ih_alarm_enter_selected_edit,
        .alarm_cancel_edit = ih_alarm_cancel_edit,
        .alarm_exit_setting = ih_alarm_exit_setting,
        .adjust_alarm = ih_adjust_alarm,
        .advance_alarm_field = ih_advance_alarm_field,
        .save_alarm_setting_and_exit = ih_save_alarm_setting_and_exit,

        .adjust_time = ih_adjust_time,
        .advance_time_field = ih_advance_time_field,
        .save_time_setting_and_exit = ih_save_time_setting_and_exit,

        .enter_time_setting_mode = ih_enter_time_setting_mode,
        .enter_alarm_setting_mode = ih_enter_alarm_setting_mode,
        .enter_timer_mode = ih_enter_timer_mode,

        .calendar_toggle_adjust_field = ih_calendar_toggle_adjust_field,
        .calendar_change_year = ih_calendar_change_year,
        .calendar_change_month = ih_calendar_change_month,
        .calendar_reset_to_current_month = ih_calendar_reset_to_current_month,
        .calendar_return_to_previous_clock = ih_calendar_return_to_previous_clock,

        .set_panel_digital = ih_set_panel_digital,
        .set_panel_analog = ih_set_panel_analog,
    };

    input_handler_state_t state = build_input_handler_state();
    input_handler_handle_button(&state, &ops, button_id, event_type);
}

/* =========================
 * app_main
 * ========================= */
void app_main(void)
{
    if (!app_bootstrap_run(&g_app_bootstrap))
    {
        ESP_LOGE(TAG, "啟動初始化流程失敗");
        return;
    }

    while (1)
    {
        app_runtime_process(&g_app_runtime);

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}