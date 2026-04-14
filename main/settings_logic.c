#include "settings_logic.h"

#include <sys/time.h>

#include "freertos/FreeRTOS.h"

#include "esp_log.h"

#include "alarm_logic.h"
#include "my_rtc.h"
#include "ui.h"

static const char *settings_logic_log_tag(const settings_logic_context_t *ctx)
{
    return (ctx != NULL && ctx->log_tag != NULL) ? ctx->log_tag : "SETTINGS";
}

static bool settings_logic_context_valid(const settings_logic_context_t *ctx)
{
    return ctx != NULL &&
           ctx->is_setting_time != NULL &&
           ctx->time_setting != NULL &&
           ctx->current_set_field != NULL &&
           ctx->current_panel != NULL &&
           ctx->time_base_valid != NULL &&
           ctx->rtc_time_valid != NULL &&
           ctx->alarm != NULL &&
           ctx->alarm_edit != NULL &&
           ctx->alarm_setting_mode != NULL &&
           ctx->alarm_set_field != NULL;
}

void settings_logic_enter_time_setting_mode(settings_logic_context_t *ctx)
{
    time_t now;

    if (!settings_logic_context_valid(ctx))
    {
        return;
    }

    now = time(NULL);

    if (localtime_r(&now, ctx->time_setting) == NULL)
    {
        ESP_LOGW(settings_logic_log_tag(ctx), "讀取目前系統時間失敗");
        *ctx->time_setting = (struct tm){0};
    }

    *ctx->is_setting_time = true;
    *ctx->current_set_field = SET_FIELD_HOUR;
    *ctx->current_panel = PANEL_DIGITAL;
    ESP_LOGI(settings_logic_log_tag(ctx), "進入時間設置模式");
}

void settings_logic_save_time_setting_and_exit(settings_logic_context_t *ctx)
{
    if (!settings_logic_context_valid(ctx))
    {
        return;
    }

    ctx->time_setting->tm_isdst = -1;

    time_t new_time = mktime(ctx->time_setting);
    if (new_time != (time_t)-1)
    {
        struct timeval tv = {
            .tv_sec = new_time,
            .tv_usec = 0};
        settimeofday(&tv, NULL);
        rtc_save_to_nvs();

        *ctx->time_base_valid = true;
        *ctx->rtc_time_valid = true;

        ESP_LOGI(settings_logic_log_tag(ctx), "時間設置已保存");
    }
    else
    {
        ESP_LOGE(settings_logic_log_tag(ctx), "mktime 失敗，未保存時間");
    }

    *ctx->is_setting_time = false;
    *ctx->current_set_field = SET_FIELD_HOUR;
    *ctx->current_panel = PANEL_DIGITAL;
}

void settings_logic_adjust_current_field(settings_logic_context_t *ctx, int delta)
{
    if (!settings_logic_context_valid(ctx))
    {
        return;
    }

    switch (*ctx->current_set_field)
    {
    case SET_FIELD_HOUR:
        ctx->time_setting->tm_hour = (ctx->time_setting->tm_hour + delta + 24) % 24;
        ESP_LOGI(settings_logic_log_tag(ctx), "設定小時: %d", ctx->time_setting->tm_hour);
        break;

    case SET_FIELD_MINUTE:
        ctx->time_setting->tm_min = (ctx->time_setting->tm_min + delta + 60) % 60;
        ESP_LOGI(settings_logic_log_tag(ctx), "設定分鐘: %d", ctx->time_setting->tm_min);
        break;

    case SET_FIELD_SECOND:
        ctx->time_setting->tm_sec = (ctx->time_setting->tm_sec + delta + 60) % 60;
        ESP_LOGI(settings_logic_log_tag(ctx), "設定秒鐘: %d", ctx->time_setting->tm_sec);
        break;
    }
}

void settings_logic_advance_setting_field(settings_logic_context_t *ctx)
{
    if (!settings_logic_context_valid(ctx))
    {
        return;
    }

    switch (*ctx->current_set_field)
    {
    case SET_FIELD_HOUR:
        *ctx->current_set_field = SET_FIELD_MINUTE;
        ESP_LOGI(settings_logic_log_tag(ctx), "切換到分鐘設定");
        break;

    case SET_FIELD_MINUTE:
        *ctx->current_set_field = SET_FIELD_SECOND;
        ESP_LOGI(settings_logic_log_tag(ctx), "切換到秒鐘設定");
        break;

    case SET_FIELD_SECOND:
        *ctx->current_set_field = SET_FIELD_HOUR;
        ESP_LOGI(settings_logic_log_tag(ctx), "切換到小時設定");
        break;
    }
}

void settings_logic_enter_alarm_setting_mode(settings_logic_context_t *ctx)
{
    if (!settings_logic_context_valid(ctx))
    {
        return;
    }

    *ctx->alarm_edit = *ctx->alarm;
    *ctx->alarm_setting_mode = true;
    *ctx->alarm_set_field = ALARM_FIELD_ENABLE;
    *ctx->current_panel = PANEL_ANALOG;

    ESP_LOGI(settings_logic_log_tag(ctx), "進入鬧鐘設定模式");

    if (ui_lock(pdMS_TO_TICKS(100)))
    {
        ui_alarm_overlay_move_foreground();
        ui_unlock();
    }

    ui_refresh();
}

void settings_logic_save_alarm_setting_and_exit(settings_logic_context_t *ctx)
{
    if (!settings_logic_context_valid(ctx))
    {
        return;
    }

    *ctx->alarm = *ctx->alarm_edit;
    *ctx->alarm_setting_mode = false;
    alarm_save_to_nvs(ctx->alarm);
    ui_refresh();
}

void settings_logic_adjust_alarm_field(settings_logic_context_t *ctx, int delta)
{
    if (!settings_logic_context_valid(ctx))
    {
        return;
    }

    switch (*ctx->alarm_set_field)
    {
    case ALARM_FIELD_ENABLE:
        ctx->alarm_edit->enabled = !ctx->alarm_edit->enabled;
        ESP_LOGI(settings_logic_log_tag(ctx), "鬧鐘啟用: %s", ctx->alarm_edit->enabled ? "ON" : "OFF");
        break;

    case ALARM_FIELD_REPEAT:
        ctx->alarm_edit->repeat = (ctx->alarm_edit->repeat == ALARM_REPEAT_ONCE) ? ALARM_REPEAT_DAILY : ALARM_REPEAT_ONCE;
        ESP_LOGI(settings_logic_log_tag(ctx), "鬧鐘週期: %s", alarm_repeat_text(ctx->alarm_edit->repeat));
        break;

    case ALARM_FIELD_HOUR:
        ctx->alarm_edit->hour = (ctx->alarm_edit->hour + delta + 24) % 24;
        ESP_LOGI(settings_logic_log_tag(ctx), "鬧鐘小時: %d", ctx->alarm_edit->hour);
        break;

    case ALARM_FIELD_MINUTE:
        ctx->alarm_edit->minute = (ctx->alarm_edit->minute + delta + 60) % 60;
        ESP_LOGI(settings_logic_log_tag(ctx), "鬧鐘分鐘: %d", ctx->alarm_edit->minute);
        break;
    }

    ui_refresh();
}

void settings_logic_advance_alarm_field(settings_logic_context_t *ctx)
{
    if (!settings_logic_context_valid(ctx))
    {
        return;
    }

    switch (*ctx->alarm_set_field)
    {
    case ALARM_FIELD_ENABLE:
        *ctx->alarm_set_field = ALARM_FIELD_REPEAT;
        break;
    case ALARM_FIELD_REPEAT:
        *ctx->alarm_set_field = ALARM_FIELD_HOUR;
        break;
    case ALARM_FIELD_HOUR:
        *ctx->alarm_set_field = ALARM_FIELD_MINUTE;
        break;
    case ALARM_FIELD_MINUTE:
    default:
        *ctx->alarm_set_field = ALARM_FIELD_ENABLE;
        break;
    }

    ui_refresh();
}