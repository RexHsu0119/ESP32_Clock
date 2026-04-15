#pragma once

#include <stdbool.h>

#include "clock_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        bool *menu_open;
        menu_item_t *menu_selected;
        int *menu_top_index;
        bool *confirm_open;
        confirm_action_t *confirm_action;
        bool *confirm_yes_selected;
        volatile bool *request_deep_sleep;
        volatile bool *request_open_wifi_setup;
        volatile bool *request_clear_wifi;
        clock_panel_t *current_panel;
        clock_panel_t *last_clock_panel_before_overlay;
        calendar_adjust_field_t *calendar_adjust_field;
        int *calendar_year;
        int *calendar_month;
        void (*enter_time_setting_mode)(void);
        void (*enter_alarm_setting_mode)(void);
        void (*start_manual_resync)(void);
    } menu_logic_context_t;

    const char *menu_logic_item_text(menu_item_t item);
    void menu_logic_open(menu_logic_context_t *ctx);
    void menu_logic_close(menu_logic_context_t *ctx);
    void menu_logic_move(menu_logic_context_t *ctx, int delta);
    void menu_logic_confirm_open(menu_logic_context_t *ctx, confirm_action_t action);
    void menu_logic_confirm_close(menu_logic_context_t *ctx);
    void menu_logic_confirm_execute(menu_logic_context_t *ctx);
    void menu_logic_execute_selected(menu_logic_context_t *ctx);

#ifdef __cplusplus
}
#endif