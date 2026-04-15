#include <stddef.h>

#include "input_handler.h"
#include "button.h"

void input_handler_handle_button(const input_handler_state_t *state,
                                 const input_handler_ops_t *ops,
                                 uint8_t button_id,
                                 uint8_t event_type)
{
    if (state == NULL || ops == NULL)
    {
        return;
    }

    if (state->alarm_ringing)
    {
        if (ops->stop_alarm != NULL)
        {
            ops->stop_alarm();
        }
        return;
    }

    if (state->wifi_portal_mode)
    {
        if (event_type == BUTTON_VERY_LONG_PRESS && button_id == BUTTON_CENTER)
        {
            if (ops->request_deep_sleep != NULL)
            {
                ops->request_deep_sleep();
            }
        }
        return;
    }

    /* Confirm 優先 */
    if (state->confirm_open)
    {
        if (event_type == BUTTON_SHORT_PRESS || event_type == BUTTON_REPEAT_PRESS)
        {
            switch (button_id)
            {
            case BUTTON_UP:
                if (ops->confirm_select_no != NULL)
                {
                    ops->confirm_select_no();
                }
                break;

            case BUTTON_DOWN:
                if (ops->confirm_select_yes != NULL)
                {
                    ops->confirm_select_yes();
                }
                break;

            case BUTTON_CENTER:
                if (event_type == BUTTON_SHORT_PRESS && ops->confirm_execute != NULL)
                {
                    ops->confirm_execute();
                }
                break;

            default:
                break;
            }
        }
        else if (event_type == BUTTON_LONG_PRESS && button_id == BUTTON_CENTER)
        {
            if (ops->confirm_close != NULL)
            {
                ops->confirm_close();
            }
        }
        return;
    }

    /* Menu 優先 */
    if (state->menu_open)
    {
        if (event_type == BUTTON_SHORT_PRESS || event_type == BUTTON_REPEAT_PRESS)
        {
            switch (button_id)
            {
            case BUTTON_UP:
                if (ops->menu_move != NULL)
                {
                    ops->menu_move(-1);
                }
                break;

            case BUTTON_DOWN:
                if (ops->menu_move != NULL)
                {
                    ops->menu_move(+1);
                }
                break;

            case BUTTON_CENTER:
                if (event_type == BUTTON_SHORT_PRESS && ops->menu_execute != NULL)
                {
                    ops->menu_execute();
                }
                break;

            default:
                break;
            }
        }
        else if (event_type == BUTTON_LONG_PRESS && button_id == BUTTON_CENTER)
        {
            if (ops->menu_close != NULL)
            {
                ops->menu_close();
            }
        }
        return;
    }

    if (state->timer_mode)
    {
        if (event_type == BUTTON_SHORT_PRESS || event_type == BUTTON_REPEAT_PRESS)
        {
            if (state->timer_done)
            {
                if (ops->timer_ack_done != NULL)
                {
                    ops->timer_ack_done();
                }
                return;
            }

            if (state->timer_set_mode)
            {
                switch (button_id)
                {
                case BUTTON_UP:
                    if (ops->timer_adjust != NULL)
                    {
                        ops->timer_adjust(+1);
                    }
                    break;
                case BUTTON_DOWN:
                    if (ops->timer_adjust != NULL)
                    {
                        ops->timer_adjust(-1);
                    }
                    break;
                case BUTTON_CENTER:
                    if (event_type == BUTTON_SHORT_PRESS && ops->timer_advance_field != NULL)
                    {
                        ops->timer_advance_field();
                    }
                    break;
                case BUTTON_COMBO_UP_DOWN:
                    if (event_type == BUTTON_SHORT_PRESS && ops->timer_clear != NULL)
                    {
                        ops->timer_clear();
                    }
                    break;
                default:
                    break;
                }
                return;
            }

            if (state->timer_running || state->timer_paused)
            {
                switch (button_id)
                {
                case BUTTON_CENTER:
                    if (event_type == BUTTON_SHORT_PRESS && ops->timer_pause_resume != NULL)
                    {
                        ops->timer_pause_resume();
                    }
                    break;
                case BUTTON_COMBO_UP_DOWN:
                    if (event_type == BUTTON_SHORT_PRESS && ops->timer_cancel != NULL)
                    {
                        ops->timer_cancel();
                    }
                    break;
                default:
                    break;
                }
                return;
            }
        }
        else if (event_type == BUTTON_LONG_PRESS && button_id == BUTTON_CENTER)
        {
            if (state->timer_set_mode)
            {
                if (ops->timer_start_or_exit != NULL)
                {
                    ops->timer_start_or_exit();
                }
            }
            else if (state->timer_running || state->timer_paused)
            {
                if (ops->timer_exit != NULL)
                {
                    ops->timer_exit();
                }
            }
            else if (state->timer_done && ops->timer_ack_done != NULL)
            {
                ops->timer_ack_done();
            }
            return;
        }

        return;
    }

    if (state->stopwatch_mode)
    {
        if (event_type == BUTTON_SHORT_PRESS || event_type == BUTTON_REPEAT_PRESS)
        {
            switch (button_id)
            {
            case BUTTON_CENTER:
                if (event_type == BUTTON_SHORT_PRESS && ops->stopwatch_toggle_start_pause != NULL)
                {
                    ops->stopwatch_toggle_start_pause();
                }
                break;
            case BUTTON_COMBO_UP_DOWN:
                if (event_type == BUTTON_SHORT_PRESS &&
                    !state->stopwatch_running &&
                    ops->stopwatch_reset != NULL)
                {
                    ops->stopwatch_reset();
                }
                break;
            default:
                break;
            }
        }
        else if (event_type == BUTTON_LONG_PRESS && button_id == BUTTON_CENTER)
        {
            if (ops->stopwatch_exit != NULL)
            {
                ops->stopwatch_exit();
            }
            return;
        }

        return;
    }

    if (event_type == BUTTON_SHORT_PRESS || event_type == BUTTON_REPEAT_PRESS)
    {
        if (state->alarm_setting_mode)
        {
            if (state->alarm_list_mode)
            {
                switch (button_id)
                {
                case BUTTON_UP:
                    if (ops->alarm_move_selection != NULL)
                    {
                        ops->alarm_move_selection(-1);
                    }
                    break;

                case BUTTON_DOWN:
                    if (ops->alarm_move_selection != NULL)
                    {
                        ops->alarm_move_selection(+1);
                    }
                    break;

                case BUTTON_CENTER:
                    if (event_type == BUTTON_SHORT_PRESS && ops->alarm_enter_selected_edit != NULL)
                    {
                        ops->alarm_enter_selected_edit();
                    }
                    break;

                case BUTTON_COMBO_UP_DOWN:
                    if (event_type == BUTTON_SHORT_PRESS && ops->alarm_toggle_selected_enabled != NULL)
                    {
                        ops->alarm_toggle_selected_enabled();
                    }
                    break;

                default:
                    break;
                }
                return;
            }

            switch (button_id)
            {
            case BUTTON_UP:
                if (ops->adjust_alarm != NULL)
                {
                    ops->adjust_alarm(+1);
                }
                break;

            case BUTTON_DOWN:
                if (ops->adjust_alarm != NULL)
                {
                    ops->adjust_alarm(-1);
                }
                break;

            case BUTTON_CENTER:
                if (event_type == BUTTON_SHORT_PRESS && ops->advance_alarm_field != NULL)
                {
                    ops->advance_alarm_field();
                }
                break;

            case BUTTON_COMBO_UP_DOWN:
                if (event_type == BUTTON_SHORT_PRESS && ops->alarm_cancel_edit != NULL)
                {
                    ops->alarm_cancel_edit();
                }
                break;

            default:
                break;
            }
            return;
        }

        if (state->time_setting_mode)
        {
            switch (button_id)
            {
            case BUTTON_UP:
                if (ops->adjust_time != NULL)
                {
                    ops->adjust_time(+1);
                }
                break;

            case BUTTON_DOWN:
                if (ops->adjust_time != NULL)
                {
                    ops->adjust_time(-1);
                }
                break;

            case BUTTON_CENTER:
                if (event_type == BUTTON_SHORT_PRESS && ops->advance_time_field != NULL)
                {
                    ops->advance_time_field();
                }
                break;

            default:
                break;
            }
            return;
        }

        if (state->calendar_active)
        {
            switch (button_id)
            {
            case BUTTON_UP:
                if (state->calendar_adjust_year_selected)
                {
                    if (ops->calendar_change_year != NULL)
                    {
                        ops->calendar_change_year(-1);
                    }
                }
                else if (ops->calendar_change_month != NULL)
                {
                    ops->calendar_change_month(-1);
                }
                break;

            case BUTTON_DOWN:
                if (state->calendar_adjust_year_selected)
                {
                    if (ops->calendar_change_year != NULL)
                    {
                        ops->calendar_change_year(+1);
                    }
                }
                else if (ops->calendar_change_month != NULL)
                {
                    ops->calendar_change_month(+1);
                }
                break;

            case BUTTON_CENTER:
                if (event_type == BUTTON_SHORT_PRESS &&
                    ops->calendar_reset_to_current_month != NULL)
                {
                    ops->calendar_reset_to_current_month();
                }
                break;

            case BUTTON_COMBO_UP_DOWN:
                if (event_type == BUTTON_SHORT_PRESS &&
                    ops->calendar_toggle_adjust_field != NULL)
                {
                    ops->calendar_toggle_adjust_field();
                }
                break;

            default:
                break;
            }
            return;
        }

        if (event_type == BUTTON_SHORT_PRESS)
        {
            switch (button_id)
            {
            case BUTTON_UP:
                if (ops->set_panel_digital != NULL)
                {
                    ops->set_panel_digital();
                }
                break;

            case BUTTON_DOWN:
                if (ops->set_panel_analog != NULL)
                {
                    ops->set_panel_analog();
                }
                break;

            case BUTTON_CENTER:
                if (ops->menu_open != NULL)
                {
                    ops->menu_open();
                }
                break;

            default:
                break;
            }
        }
    }
    else if (event_type == BUTTON_LONG_PRESS)
    {
        if (button_id == BUTTON_CENTER)
        {
            if (state->alarm_setting_mode)
            {
                if (state->alarm_edit_mode)
                {
                    if (ops->save_alarm_setting_and_exit != NULL)
                    {
                        ops->save_alarm_setting_and_exit();
                    }
                }
                else if (ops->alarm_exit_setting != NULL)
                {
                    ops->alarm_exit_setting();
                }
            }
            else if (!state->time_setting_mode)
            {
                if (state->calendar_active)
                {
                    if (ops->calendar_return_to_previous_clock != NULL)
                    {
                        ops->calendar_return_to_previous_clock();
                    }
                }
                else if (state->digital_active)
                {
                    if (ops->enter_time_setting_mode != NULL)
                    {
                        ops->enter_time_setting_mode();
                    }
                }
                else if (state->analog_active)
                {
                    if (ops->enter_alarm_setting_mode != NULL)
                    {
                        ops->enter_alarm_setting_mode();
                    }
                }
            }
            else
            {
                if (ops->save_time_setting_and_exit != NULL)
                {
                    ops->save_time_setting_and_exit();
                }
            }
        }
    }
    else if (event_type == BUTTON_VERY_LONG_PRESS)
    {
        if (button_id == BUTTON_CENTER)
        {
            if (ops->request_deep_sleep != NULL)
            {
                ops->request_deep_sleep();
            }
        }
    }
}