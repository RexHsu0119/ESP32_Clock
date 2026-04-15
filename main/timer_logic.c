#include "timer_logic.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "ui.h"

static const char *timer_logic_log_tag(const timer_logic_context_t *ctx)
{
    return (ctx != NULL && ctx->log_tag != NULL) ? ctx->log_tag : "TIMER";
}

static bool timer_logic_context_valid(const timer_logic_context_t *ctx)
{
    return ctx != NULL &&
           ctx->timer_mode != NULL &&
           ctx->timer_state != NULL &&
           ctx->timer_set_field != NULL &&
           ctx->timer_hours != NULL &&
           ctx->timer_minutes != NULL &&
           ctx->timer_seconds != NULL &&
           ctx->timer_remaining_seconds != NULL &&
           ctx->timer_end_time_us != NULL &&
           ctx->current_panel != NULL &&
           ctx->last_clock_panel_before_overlay != NULL;
}

static void timer_logic_start_done_alert(timer_logic_context_t *ctx)
{
    if (ctx != NULL && ctx->start_done_alert != NULL)
    {
        ctx->start_done_alert();
    }
}

static void timer_logic_stop_done_alert(timer_logic_context_t *ctx)
{
    if (ctx != NULL && ctx->stop_done_alert != NULL)
    {
        ctx->stop_done_alert();
    }
}

static int32_t timer_logic_total_seconds(const timer_logic_context_t *ctx)
{
    return (*ctx->timer_hours * 3600) + (*ctx->timer_minutes * 60) + *ctx->timer_seconds;
}

static void timer_logic_set_from_seconds(timer_logic_context_t *ctx, int32_t seconds)
{
    if (!timer_logic_context_valid(ctx))
    {
        return;
    }

    if (seconds < 0)
    {
        seconds = 0;
    }

    *ctx->timer_hours = seconds / 3600;
    seconds %= 3600;
    *ctx->timer_minutes = seconds / 60;
    *ctx->timer_seconds = seconds % 60;
}

void timer_logic_enter_mode(timer_logic_context_t *ctx)
{
    if (!timer_logic_context_valid(ctx))
    {
        return;
    }

    *ctx->last_clock_panel_before_overlay = *ctx->current_panel;
    *ctx->timer_mode = true;

    if (*ctx->timer_state == TIMER_STATE_IDLE)
    {
        *ctx->timer_state = TIMER_STATE_SET;
    }

    ui_refresh();
}

void timer_logic_exit_mode(timer_logic_context_t *ctx)
{
    if (!timer_logic_context_valid(ctx))
    {
        return;
    }

    timer_logic_stop_done_alert(ctx);
    *ctx->timer_mode = false;
    *ctx->current_panel = *ctx->last_clock_panel_before_overlay;
    ui_refresh();
}

void timer_logic_adjust_field(timer_logic_context_t *ctx, int delta)
{
    if (!timer_logic_context_valid(ctx) || *ctx->timer_state != TIMER_STATE_SET)
    {
        return;
    }

    switch (*ctx->timer_set_field)
    {
    case TIMER_FIELD_HOUR:
        *ctx->timer_hours = (*ctx->timer_hours + delta + 100) % 100;
        break;
    case TIMER_FIELD_MINUTE:
        *ctx->timer_minutes = (*ctx->timer_minutes + delta + 60) % 60;
        break;
    case TIMER_FIELD_SECOND:
        *ctx->timer_seconds = (*ctx->timer_seconds + delta + 60) % 60;
        break;
    }

    ui_refresh();
}

void timer_logic_advance_field(timer_logic_context_t *ctx)
{
    if (!timer_logic_context_valid(ctx) || *ctx->timer_state != TIMER_STATE_SET)
    {
        return;
    }

    switch (*ctx->timer_set_field)
    {
    case TIMER_FIELD_HOUR:
        *ctx->timer_set_field = TIMER_FIELD_MINUTE;
        break;
    case TIMER_FIELD_MINUTE:
        *ctx->timer_set_field = TIMER_FIELD_SECOND;
        break;
    case TIMER_FIELD_SECOND:
    default:
        *ctx->timer_set_field = TIMER_FIELD_HOUR;
        break;
    }

    ui_refresh();
}

void timer_logic_clear(timer_logic_context_t *ctx)
{
    if (!timer_logic_context_valid(ctx))
    {
        return;
    }

    timer_logic_stop_done_alert(ctx);
    *ctx->timer_state = TIMER_STATE_SET;
    *ctx->timer_set_field = TIMER_FIELD_HOUR;
    *ctx->timer_remaining_seconds = 0;
    *ctx->timer_end_time_us = 0;
    *ctx->timer_hours = 0;
    *ctx->timer_minutes = 0;
    *ctx->timer_seconds = 0;
    *ctx->timer_mode = true;
    ui_refresh();
}

void timer_logic_start_or_exit(timer_logic_context_t *ctx)
{
    int32_t total_seconds;

    if (!timer_logic_context_valid(ctx) || *ctx->timer_state != TIMER_STATE_SET)
    {
        return;
    }

    timer_logic_stop_done_alert(ctx);
    total_seconds = timer_logic_total_seconds(ctx);
    if (total_seconds <= 0)
    {
        timer_logic_exit_mode(ctx);
        return;
    }

    *ctx->timer_remaining_seconds = total_seconds;
    *ctx->timer_end_time_us = esp_timer_get_time() + ((int64_t)total_seconds * 1000000LL);
    *ctx->timer_state = TIMER_STATE_RUNNING;
    *ctx->timer_mode = false;
    *ctx->current_panel = *ctx->last_clock_panel_before_overlay;

    ESP_LOGI(timer_logic_log_tag(ctx), "開始倒數: %02d:%02d:%02d",
             *ctx->timer_hours,
             *ctx->timer_minutes,
             *ctx->timer_seconds);

    ui_refresh();
}

void timer_logic_pause_resume(timer_logic_context_t *ctx)
{
    int64_t diff_us;
    int32_t remaining_seconds;

    if (!timer_logic_context_valid(ctx))
    {
        return;
    }

    if (*ctx->timer_state == TIMER_STATE_RUNNING)
    {
        diff_us = *ctx->timer_end_time_us - esp_timer_get_time();
        remaining_seconds = (diff_us > 0) ? (int32_t)((diff_us + 999999LL) / 1000000LL) : 0;

        if (remaining_seconds <= 0)
        {
            *ctx->timer_remaining_seconds = 0;
            *ctx->timer_state = TIMER_STATE_DONE;
            *ctx->timer_mode = true;
            timer_logic_start_done_alert(ctx);
            ui_refresh();
            return;
        }

        *ctx->timer_remaining_seconds = remaining_seconds;
        *ctx->timer_state = TIMER_STATE_PAUSED;
        timer_logic_set_from_seconds(ctx, *ctx->timer_remaining_seconds);
    }
    else if (*ctx->timer_state == TIMER_STATE_PAUSED && *ctx->timer_remaining_seconds > 0)
    {
        *ctx->timer_end_time_us = esp_timer_get_time() + ((int64_t)(*ctx->timer_remaining_seconds) * 1000000LL);
        *ctx->timer_state = TIMER_STATE_RUNNING;
    }

    ui_refresh();
}

void timer_logic_cancel(timer_logic_context_t *ctx)
{
    if (!timer_logic_context_valid(ctx))
    {
        return;
    }

    timer_logic_stop_done_alert(ctx);
    *ctx->timer_state = TIMER_STATE_SET;
    *ctx->timer_set_field = TIMER_FIELD_HOUR;
    *ctx->timer_remaining_seconds = 0;
    *ctx->timer_end_time_us = 0;
    *ctx->timer_hours = 0;
    *ctx->timer_minutes = 0;
    *ctx->timer_seconds = 0;
    *ctx->timer_mode = true;
    ui_refresh();
}

void timer_logic_ack_done(timer_logic_context_t *ctx)
{
    if (!timer_logic_context_valid(ctx) || *ctx->timer_state != TIMER_STATE_DONE)
    {
        return;
    }

    timer_logic_stop_done_alert(ctx);
    *ctx->timer_state = TIMER_STATE_SET;
    *ctx->timer_set_field = TIMER_FIELD_HOUR;
    *ctx->timer_remaining_seconds = 0;
    *ctx->timer_end_time_us = 0;
    *ctx->timer_hours = 0;
    *ctx->timer_minutes = 0;
    *ctx->timer_seconds = 0;
    *ctx->timer_mode = false;
    *ctx->current_panel = *ctx->last_clock_panel_before_overlay;
    ui_refresh();
}

void timer_logic_update(timer_logic_context_t *ctx)
{
    int64_t now_us;
    int64_t diff_us;
    int32_t remaining_seconds;

    if (!timer_logic_context_valid(ctx) || *ctx->timer_state != TIMER_STATE_RUNNING)
    {
        return;
    }

    now_us = esp_timer_get_time();
    diff_us = *ctx->timer_end_time_us - now_us;
    remaining_seconds = (diff_us > 0) ? (int32_t)((diff_us + 999999LL) / 1000000LL) : 0;

    if (remaining_seconds != *ctx->timer_remaining_seconds)
    {
        *ctx->timer_remaining_seconds = remaining_seconds;
        if (*ctx->timer_mode)
        {
            ui_refresh();
        }
    }

    if (remaining_seconds <= 0)
    {
        *ctx->timer_remaining_seconds = 0;
        *ctx->timer_state = TIMER_STATE_DONE;
        *ctx->timer_mode = true;
        *ctx->current_panel = *ctx->last_clock_panel_before_overlay;
        timer_logic_start_done_alert(ctx);
        ESP_LOGI(timer_logic_log_tag(ctx), "倒數結束");
        ui_refresh();
    }
}