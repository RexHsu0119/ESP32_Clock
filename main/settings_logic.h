#pragma once

#include <stdbool.h>
#include <time.h>

#include "clock_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        const char *log_tag;
        bool *is_setting_time;
        struct tm *time_setting;
        time_set_field_t *current_set_field;
        clock_panel_t *current_panel;
        bool *time_base_valid;
        bool *rtc_time_valid;
        alarm_config_t *alarm;
        alarm_config_t *alarm_edit;
        bool *alarm_setting_mode;
        alarm_set_field_t *alarm_set_field;
    } settings_logic_context_t;

    void settings_logic_enter_time_setting_mode(settings_logic_context_t *ctx);
    void settings_logic_save_time_setting_and_exit(settings_logic_context_t *ctx);
    void settings_logic_adjust_current_field(settings_logic_context_t *ctx, int delta);
    void settings_logic_advance_setting_field(settings_logic_context_t *ctx);

    void settings_logic_enter_alarm_setting_mode(settings_logic_context_t *ctx);
    void settings_logic_save_alarm_setting_and_exit(settings_logic_context_t *ctx);
    void settings_logic_adjust_alarm_field(settings_logic_context_t *ctx, int delta);
    void settings_logic_advance_alarm_field(settings_logic_context_t *ctx);

#ifdef __cplusplus
}
#endif