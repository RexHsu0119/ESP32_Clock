#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "clock_types.h"
#include "wifi_config.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        bool is_setting_time;
        struct tm time_setting;
        clock_panel_t current_panel;
        time_set_field_t current_set_field;
        app_mode_t app_mode;
        boot_hint_t boot_hint;

        alarm_config_t alarms[ALARM_SLOT_COUNT];
        alarm_config_t alarm_edit;
        bool alarm_setting_mode;
        alarm_ui_mode_t alarm_ui_mode;
        int selected_alarm_index;
        alarm_set_field_t alarm_set_field;
        volatile bool alarm_ringing;
        bool alarm_flash_on;
        int64_t alarm_last_flash_us;
        TaskHandle_t alarm_sound_task_handle;

        int alarm_last_trigger_year[ALARM_SLOT_COUNT];
        int alarm_last_trigger_yday[ALARM_SLOT_COUNT];
        int alarm_last_trigger_hour[ALARM_SLOT_COUNT];
        int alarm_last_trigger_minute[ALARM_SLOT_COUNT];

        bool time_base_valid;
        bool force_unknown_during_sync;

        volatile bool request_deep_sleep;

        char wifi_ssid[WIFI_CONFIG_SSID_MAX_LEN + 1];
        char wifi_password[WIFI_CONFIG_PASSWORD_MAX_LEN + 1];
        bool wifi_credentials_loaded;

        bool menu_open;
        menu_item_t menu_selected;
        int menu_top_index;
        volatile bool request_open_wifi_setup;
        volatile bool request_clear_wifi;
        clock_panel_t last_clock_panel_before_overlay;

        bool confirm_open;
        confirm_action_t confirm_action;
        bool confirm_yes_selected;

        calendar_adjust_field_t calendar_adjust_field;
        int calendar_year;
        int calendar_month;

        SemaphoreHandle_t lvgl_mutex;

        volatile bool time_syncing;
        volatile bool wifi_failed;
    } app_context_t;

    extern app_context_t g_app;
    extern bool g_rtc_time_valid;

#ifdef __cplusplus
}
#endif