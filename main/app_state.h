#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        CLOCK_FACE_DIGITAL = 0,
        CLOCK_FACE_ANALOG,
    } clock_face_t;

    typedef enum
    {
        UI_MODE_CLOCK = 0,
        UI_MODE_MENU,
        UI_MODE_CALENDAR,
        UI_MODE_TIME_SET,
        UI_MODE_ALARM_SET,
        UI_MODE_WIFI_SETUP,
        UI_MODE_CONFIRM,
    } ui_mode_t;

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
        MENU_ITEM_ALARM = 0,
        MENU_ITEM_SET_TIME,
        MENU_ITEM_CALENDAR,
        MENU_ITEM_WIFI_SETUP,
        MENU_ITEM_CLEAR_WIFI,
        MENU_ITEM_SYNC_NOW,
        MENU_ITEM_DEEP_SLEEP,
        MENU_ITEM_COUNT,
    } menu_item_t;

    typedef enum
    {
        CONFIRM_NONE = 0,
        CONFIRM_CLEAR_WIFI,
        CONFIRM_DEEP_SLEEP,
    } confirm_action_t;

    typedef enum
    {
        SET_FIELD_HOUR = 0,
        SET_FIELD_MINUTE,
        SET_FIELD_SECOND,
    } time_set_field_t;

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

    typedef struct
    {
        bool open;
        menu_item_t selected;
        int top_index;
    } menu_state_t;

    typedef struct
    {
        bool open;
        confirm_action_t action;
        bool yes_selected;
    } confirm_state_t;

    typedef struct
    {
        int year;
        int month; /* 1~12 */
    } calendar_state_t;

    typedef struct
    {
        bool request_deep_sleep;
        bool request_open_wifi_setup;
        bool request_clear_wifi;
    } request_state_t;

    typedef struct
    {
        /* app / ui mode */
        app_mode_t app_mode;
        ui_mode_t ui_mode;
        clock_face_t clock_face;
        boot_hint_t boot_hint;

        /* time / sync */
        bool time_valid;
        bool force_unknown_during_sync;
        bool time_syncing;
        bool wifi_failed;
        struct tm time_setting;
        time_set_field_t time_set_field;

        /* alarm */
        alarm_config_t alarm;
        alarm_config_t alarm_edit;
        alarm_set_field_t alarm_field;
        bool alarm_setting_mode;
        bool alarm_ringing;
        bool alarm_flash_on;
        int64_t alarm_last_flash_us;

        /* menu / confirm / calendar */
        menu_state_t menu;
        confirm_state_t confirm;
        calendar_state_t calendar;

        /* remember previous clock face before entering calendar */
        clock_face_t prev_face_before_calendar;

        /* wifi credentials cache */
        char wifi_ssid[33];
        char wifi_password[65];
        bool wifi_credentials_loaded;

        /* requests for main loop */
        request_state_t req;

        /* shared objects */
        SemaphoreHandle_t lvgl_mutex;
    } app_context_t;

    extern app_context_t g_app;

    /* Deep Sleep 後保留的旗標 */
    extern bool g_rtc_time_valid;

#ifdef __cplusplus
}
#endif