#include "app_state.h"
#include "esp_attr.h"

app_context_t g_app = {
    .app_mode = APP_MODE_CLOCK,
    .ui_mode = UI_MODE_CLOCK,
    .clock_face = CLOCK_FACE_DIGITAL,
    .boot_hint = BOOT_HINT_NONE,

    .time_valid = false,
    .force_unknown_during_sync = true,
    .time_syncing = false,
    .wifi_failed = false,
    .time_setting = {0},
    .time_set_field = SET_FIELD_HOUR,

    .alarm = {
        .enabled = false,
        .hour = 7,
        .minute = 0,
        .repeat = ALARM_REPEAT_DAILY,
    },
    .alarm_edit = {0},
    .alarm_field = ALARM_FIELD_ENABLE,
    .alarm_setting_mode = false,
    .alarm_ringing = false,
    .alarm_flash_on = false,
    .alarm_last_flash_us = 0,

    .menu = {
        .open = false,
        .selected = MENU_ITEM_ALARM,
        .top_index = 0,
    },

    .confirm = {
        .open = false,
        .action = CONFIRM_NONE,
        .yes_selected = false,
    },

    .calendar = {
        .year = 0,
        .month = 0,
    },

    .prev_face_before_calendar = CLOCK_FACE_DIGITAL,

    .wifi_ssid = {0},
    .wifi_password = {0},
    .wifi_credentials_loaded = false,

    .req = {
        .request_deep_sleep = false,
        .request_open_wifi_setup = false,
        .request_clear_wifi = false,
    },

    .lvgl_mutex = NULL,
};

/* Deep Sleep 後保留 */
RTC_DATA_ATTR bool g_rtc_time_valid = false;