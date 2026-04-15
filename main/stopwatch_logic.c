#include "stopwatch_logic.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "ui.h"

static const char *stopwatch_logic_log_tag(const stopwatch_logic_context_t *ctx)
{
    return (ctx != NULL && ctx->log_tag != NULL) ? ctx->log_tag : "STOPWATCH";
}

static bool stopwatch_logic_context_valid(const stopwatch_logic_context_t *ctx)
{
    return ctx != NULL &&
           ctx->stopwatch_mode != NULL &&
           ctx->stopwatch_state != NULL &&
           ctx->stopwatch_elapsed_ms != NULL &&
           ctx->stopwatch_start_time_us != NULL &&
           ctx->current_panel != NULL &&
           ctx->last_clock_panel_before_overlay != NULL;
}

static int64_t stopwatch_logic_current_elapsed_ms(const stopwatch_logic_context_t *ctx)
{
    int64_t now_us;

    if (!stopwatch_logic_context_valid(ctx))
    {
        return 0;
    }

    if (*ctx->stopwatch_state != STOPWATCH_STATE_RUNNING)
    {
        return *ctx->stopwatch_elapsed_ms;
    }

    now_us = esp_timer_get_time();
    return (now_us - *ctx->stopwatch_start_time_us) / 1000LL;
}

void stopwatch_logic_enter_mode(stopwatch_logic_context_t *ctx)
{
    if (!stopwatch_logic_context_valid(ctx))
    {
        return;
    }

    *ctx->last_clock_panel_before_overlay = *ctx->current_panel;
    *ctx->stopwatch_mode = true;
    ui_refresh();
}

void stopwatch_logic_exit_mode(stopwatch_logic_context_t *ctx)
{
    if (!stopwatch_logic_context_valid(ctx))
    {
        return;
    }

    *ctx->stopwatch_mode = false;
    *ctx->current_panel = *ctx->last_clock_panel_before_overlay;
    ui_refresh();
}

void stopwatch_logic_toggle_start_pause(stopwatch_logic_context_t *ctx)
{
    int64_t now_us;

    if (!stopwatch_logic_context_valid(ctx))
    {
        return;
    }

    now_us = esp_timer_get_time();

    if (*ctx->stopwatch_state == STOPWATCH_STATE_RUNNING)
    {
        *ctx->stopwatch_elapsed_ms = (now_us - *ctx->stopwatch_start_time_us) / 1000LL;
        *ctx->stopwatch_state = STOPWATCH_STATE_PAUSED;
        ESP_LOGI(stopwatch_logic_log_tag(ctx), "碼錶暫停: %lld ms", *ctx->stopwatch_elapsed_ms);
    }
    else
    {
        *ctx->stopwatch_start_time_us = now_us - (*ctx->stopwatch_elapsed_ms * 1000LL);
        *ctx->stopwatch_state = STOPWATCH_STATE_RUNNING;
        ESP_LOGI(stopwatch_logic_log_tag(ctx), "碼錶開始/繼續");
    }

    *ctx->stopwatch_mode = true;
    ui_refresh();
}

void stopwatch_logic_reset(stopwatch_logic_context_t *ctx)
{
    if (!stopwatch_logic_context_valid(ctx) || *ctx->stopwatch_state == STOPWATCH_STATE_RUNNING)
    {
        return;
    }

    *ctx->stopwatch_elapsed_ms = 0;
    *ctx->stopwatch_start_time_us = 0;
    *ctx->stopwatch_state = STOPWATCH_STATE_IDLE;
    *ctx->stopwatch_mode = true;
    ui_refresh();
}

void stopwatch_logic_update(stopwatch_logic_context_t *ctx)
{
    if (!stopwatch_logic_context_valid(ctx) || *ctx->stopwatch_state != STOPWATCH_STATE_RUNNING)
    {
        return;
    }

    *ctx->stopwatch_elapsed_ms = stopwatch_logic_current_elapsed_ms(ctx);
}