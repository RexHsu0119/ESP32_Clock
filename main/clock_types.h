#pragma once

#include <stdbool.h>

#define ALARM_SLOT_COUNT 3

typedef enum
{
    PANEL_DIGITAL = 0,
    PANEL_ANALOG,
    PANEL_CALENDAR,
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
    ALARM_FIELD_STATUS = 0,
    ALARM_FIELD_HOUR,
    ALARM_FIELD_MINUTE,
} alarm_set_field_t;

typedef enum
{
    ALARM_UI_LIST = 0,
    ALARM_UI_EDIT,
} alarm_ui_mode_t;

typedef enum
{
    TIMER_FIELD_HOUR = 0,
    TIMER_FIELD_MINUTE,
    TIMER_FIELD_SECOND,
} timer_set_field_t;

typedef enum
{
    TIMER_STATE_IDLE = 0,
    TIMER_STATE_SET,
    TIMER_STATE_RUNNING,
    TIMER_STATE_PAUSED,
    TIMER_STATE_DONE,
} timer_state_t;

typedef enum
{
    STOPWATCH_STATE_IDLE = 0,
    STOPWATCH_STATE_RUNNING,
    STOPWATCH_STATE_PAUSED,
} stopwatch_state_t;

typedef enum
{
    MENU_ITEM_ALARM = 0,
    MENU_ITEM_TIMER,
    MENU_ITEM_STOPWATCH,
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
    CALENDAR_ADJUST_MONTH = 0,
    CALENDAR_ADJUST_YEAR,
} calendar_adjust_field_t;

typedef enum
{
    CONFIRM_NONE = 0,
    CONFIRM_CLEAR_WIFI,
} confirm_action_t;

typedef struct
{
    bool enabled;
    int hour;
    int minute;
    alarm_repeat_t repeat;
} alarm_config_t;