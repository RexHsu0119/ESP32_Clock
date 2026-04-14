#include "app_state.h"

#include "esp_attr.h"

app_context_t g_app = {
    .is_setting_time = false,
    .time_setting = {0},
    .current_panel = PANEL_DIGITAL,
    .current_set_field = SET_FIELD_HOUR,
    .app_mode = APP_MODE_CLOCK,
    .boot_hint = BOOT_HINT_NONE,

    .alarm = {
        .enabled = false,
        .hour = 7,
        .minute = 0,
        .repeat = ALARM_REPEAT_DAILY,
    },
    .alarm_edit = {0},
    .alarm_setting_mode = false,
    .alarm_set_field = ALARM_FIELD_ENABLE,
    .alarm_ringing = false,
    .alarm_flash_on = false,
    .alarm_last_flash_us = 0,
    .alarm_sound_task_handle = NULL,

    .alarm_last_trigger_year = -1,
    .alarm_last_trigger_yday = -1,
    .alarm_last_trigger_hour = -1,
    .alarm_last_trigger_minute = -1,

    .time_base_valid = false,
    .force_unknown_during_sync = true,

    .request_deep_sleep = false,

    .wifi_ssid = {0},
    .wifi_password = {0},
    .wifi_credentials_loaded = false,

    .menu_open = false,
    .menu_selected = MENU_ITEM_ALARM,
    .menu_top_index = 0,
    .request_open_wifi_setup = false,
    .request_clear_wifi = false,
    .last_clock_panel_before_calendar = PANEL_DIGITAL,

    .confirm_open = false,
    .confirm_action = CONFIRM_NONE,
    .confirm_yes_selected = false,

    .calendar_year = 0,
    .calendar_month = 0,

    .lvgl_mutex = NULL,

    .time_syncing = false,
    .wifi_failed = false,
};

RTC_DATA_ATTR bool g_rtc_time_valid = false;