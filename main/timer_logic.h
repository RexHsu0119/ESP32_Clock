#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "clock_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        const char *log_tag;
        bool *timer_mode;
        timer_state_t *timer_state;
        timer_set_field_t *timer_set_field;
        int *timer_hours;
        int *timer_minutes;
        int *timer_seconds;
        int32_t *timer_remaining_seconds;
        int64_t *timer_end_time_us;
        clock_panel_t *current_panel;
        clock_panel_t *last_clock_panel_before_overlay;
        void (*start_done_alert)(void);
        void (*stop_done_alert)(void);
    } timer_logic_context_t;

    void timer_logic_enter_mode(timer_logic_context_t *ctx);
    void timer_logic_exit_mode(timer_logic_context_t *ctx);
    void timer_logic_adjust_field(timer_logic_context_t *ctx, int delta);
    void timer_logic_advance_field(timer_logic_context_t *ctx);
    void timer_logic_clear(timer_logic_context_t *ctx);
    void timer_logic_start_or_exit(timer_logic_context_t *ctx);
    void timer_logic_pause_resume(timer_logic_context_t *ctx);
    void timer_logic_cancel(timer_logic_context_t *ctx);
    void timer_logic_ack_done(timer_logic_context_t *ctx);
    void timer_logic_update(timer_logic_context_t *ctx);

#ifdef __cplusplus
}
#endif