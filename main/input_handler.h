#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        bool alarm_ringing;
        bool wifi_portal_mode;
        bool confirm_open;
        bool menu_open;
        bool alarm_setting_mode;
        bool alarm_list_mode;
        bool alarm_edit_mode;
        bool timer_mode;
        bool timer_set_mode;
        bool timer_running;
        bool timer_paused;
        bool timer_done;
        bool stopwatch_mode;
        bool stopwatch_running;
        bool stopwatch_paused;
        bool time_setting_mode;
        bool calendar_active;
        bool calendar_adjust_year_selected;
        bool digital_active;
        bool analog_active;
    } input_handler_state_t;

    typedef struct
    {
        void (*stop_alarm)(void);
        void (*request_deep_sleep)(void);

        void (*confirm_select_no)(void);
        void (*confirm_select_yes)(void);
        void (*confirm_execute)(void);
        void (*confirm_close)(void);

        void (*menu_move)(int delta);
        void (*menu_execute)(void);
        void (*menu_close)(void);
        void (*menu_open)(void);

        void (*timer_adjust)(int delta);
        void (*timer_advance_field)(void);
        void (*timer_clear)(void);
        void (*timer_start_or_exit)(void);
        void (*timer_pause_resume)(void);
        void (*timer_cancel)(void);
        void (*timer_exit)(void);
        void (*timer_ack_done)(void);

        void (*stopwatch_toggle_start_pause)(void);
        void (*stopwatch_reset)(void);
        void (*stopwatch_exit)(void);

        void (*alarm_move_selection)(int delta);
        void (*alarm_toggle_selected_enabled)(void);
        void (*alarm_enter_selected_edit)(void);
        void (*alarm_cancel_edit)(void);
        void (*alarm_exit_setting)(void);
        void (*adjust_alarm)(int delta);
        void (*advance_alarm_field)(void);
        void (*save_alarm_setting_and_exit)(void);

        void (*adjust_time)(int delta);
        void (*advance_time_field)(void);
        void (*save_time_setting_and_exit)(void);

        void (*enter_time_setting_mode)(void);
        void (*enter_alarm_setting_mode)(void);
        void (*enter_timer_mode)(void);

        void (*calendar_toggle_adjust_field)(void);
        void (*calendar_change_year)(int delta);
        void (*calendar_change_month)(int delta);
        void (*calendar_reset_to_current_month)(void);
        void (*calendar_return_to_previous_clock)(void);

        void (*set_panel_digital)(void);
        void (*set_panel_analog)(void);
    } input_handler_ops_t;

    void input_handler_handle_button(const input_handler_state_t *state,
                                     const input_handler_ops_t *ops,
                                     uint8_t button_id,
                                     uint8_t event_type);

#ifdef __cplusplus
}
#endif