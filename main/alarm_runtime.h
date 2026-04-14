#pragma once

#include <stdint.h>

#include "app_state.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        const char *log_tag;
        app_context_t *app;
        int64_t flash_period_us;
    } alarm_runtime_context_t;

    void alarm_runtime_apply_background_state_locked(const alarm_runtime_context_t *ctx);
    void alarm_runtime_stop(const alarm_runtime_context_t *ctx);
    void alarm_runtime_check_trigger(const alarm_runtime_context_t *ctx);
    void alarm_runtime_update_flash_effect(const alarm_runtime_context_t *ctx);

#ifdef __cplusplus
}
#endif