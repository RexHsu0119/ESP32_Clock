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
        bool *stopwatch_mode;
        stopwatch_state_t *stopwatch_state;
        int64_t *stopwatch_elapsed_ms;
        int64_t *stopwatch_start_time_us;
        clock_panel_t *current_panel;
        clock_panel_t *last_clock_panel_before_overlay;
    } stopwatch_logic_context_t;

    void stopwatch_logic_enter_mode(stopwatch_logic_context_t *ctx);
    void stopwatch_logic_exit_mode(stopwatch_logic_context_t *ctx);
    void stopwatch_logic_toggle_start_pause(stopwatch_logic_context_t *ctx);
    void stopwatch_logic_reset(stopwatch_logic_context_t *ctx);
    void stopwatch_logic_update(stopwatch_logic_context_t *ctx);

#ifdef __cplusplus
}
#endif