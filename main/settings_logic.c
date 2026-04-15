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
           ctx->last_clock_panel_before_overlay != NULL &&
           ctx->time_base_valid != NULL &&
           ctx->rtc_time_valid != NULL &&
           ctx->alarms != NULL &&
           ctx->alarm_count > 0 &&
           ctx->alarm_edit != NULL &&
           ctx->alarm_setting_mode != NULL &&
           ctx->alarm_ui_mode != NULL &&
           ctx->selected_alarm_index != NULL &&
           ctx->alarm_set_field != NULL;
}

static int settings_logic_clamp_alarm_index(const settings_logic_context_t *ctx, int index)
{
    if (ctx == NULL || ctx->alarm_count <= 0)
    {
        return 0;
    }

    if (index < 0)
    {
        return 0;
    }

    if (index >= ctx->alarm_count)
    {
        return ctx->alarm_count - 1;
    }

    return index;
}

static const char *settings_logic_alarm_status_text(const alarm_config_t *alarm)
{
    if (alarm == NULL || !alarm->enabled)
    {
        return "OFF";
    }

    return alarm_repeat_text(alarm->repeat);
}

static void settings_logic_cycle_alarm_status(alarm_config_t *alarm, int delta)
{
    if (alarm == NULL || delta == 0)
    {
        return;
    }

    int direction = (delta > 0) ? 1 : -1;
    int state;

    if (!alarm->enabled)
    {
        state = 0;
    }
    else if (alarm->repeat == ALARM_REPEAT_ONCE)
    {
        state = 1;
    }
    else
    {
        state = 2;
    }

    state = (state + direction + 3) % 3;

    switch (state)
    {
    case 0:
        alarm->enabled = false;
        alarm->repeat = ALARM_REPEAT_ONCE;
        break;
    case 1:
        alarm->enabled = true;
        alarm->repeat = ALARM_REPEAT_ONCE;
        break;
    case 2:
    default:
        alarm->enabled = true;
        alarm->repeat = ALARM_REPEAT_DAILY;
        break;
    }
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

    *ctx->last_clock_panel_before_overlay = *ctx->current_panel;
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
    *ctx->current_panel = *ctx->last_clock_panel_before_overlay;
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

    *ctx->last_clock_panel_before_overlay = *ctx->current_panel;
    *ctx->alarm_setting_mode = true;
    *ctx->alarm_ui_mode = ALARM_UI_LIST;
    *ctx->selected_alarm_index = settings_logic_clamp_alarm_index(ctx, *ctx->selected_alarm_index);
    *ctx->alarm_set_field = ALARM_FIELD_STATUS;
    *ctx->current_panel = PANEL_ANALOG;

    ESP_LOGI(settings_logic_log_tag(ctx), "進入鬧鐘清單模式");

    if (ui_lock(pdMS_TO_TICKS(100)))
    {
        ui_alarm_overlay_move_foreground();
        ui_unlock();
    }

    ui_refresh();
}

void settings_logic_exit_alarm_setting_mode(settings_logic_context_t *ctx)
{
    if (!settings_logic_context_valid(ctx))
    {
        return;
    }

    *ctx->alarm_setting_mode = false;
    *ctx->alarm_ui_mode = ALARM_UI_LIST;
    *ctx->alarm_set_field = ALARM_FIELD_STATUS;
    *ctx->current_panel = *ctx->last_clock_panel_before_overlay;
    ui_refresh();
}

void settings_logic_move_alarm_selection(settings_logic_context_t *ctx, int delta)
{
    if (!settings_logic_context_valid(ctx) || *ctx->alarm_ui_mode != ALARM_UI_LIST)
    {
        return;
    }

    int next = *ctx->selected_alarm_index + delta;
    if (next < 0)
    {
        next = ctx->alarm_count - 1;
    }
    else if (next >= ctx->alarm_count)
    {
        next = 0;
    }

    *ctx->selected_alarm_index = next;
    ui_refresh();
}

void settings_logic_toggle_selected_alarm_enabled(settings_logic_context_t *ctx)
{
    if (!settings_logic_context_valid(ctx) || *ctx->alarm_ui_mode != ALARM_UI_LIST)
    {
        return;
    }

    int index = settings_logic_clamp_alarm_index(ctx, *ctx->selected_alarm_index);
    settings_logic_cycle_alarm_status(&ctx->alarms[index], +1);
    alarm_save_all_to_nvs(ctx->alarms, ctx->alarm_count);

    ESP_LOGI(settings_logic_log_tag(ctx), "切換鬧鐘 A%d 狀態: %s",
             index + 1,
             settings_logic_alarm_status_text(&ctx->alarms[index]));

    ui_refresh();
}

void settings_logic_enter_selected_alarm_edit(settings_logic_context_t *ctx)
{
    if (!settings_logic_context_valid(ctx) || *ctx->alarm_ui_mode != ALARM_UI_LIST)
    {
        return;
    }

    int index = settings_logic_clamp_alarm_index(ctx, *ctx->selected_alarm_index);
    *ctx->alarm_edit = ctx->alarms[index];
    *ctx->alarm_ui_mode = ALARM_UI_EDIT;
    *ctx->alarm_set_field = ALARM_FIELD_STATUS;

    ESP_LOGI(settings_logic_log_tag(ctx), "編輯鬧鐘 A%d", index + 1);
    ui_refresh();
}

void settings_logic_cancel_alarm_edit(settings_logic_context_t *ctx)
{
    if (!settings_logic_context_valid(ctx) || *ctx->alarm_ui_mode != ALARM_UI_EDIT)
    {
        return;
    }

    *ctx->alarm_ui_mode = ALARM_UI_LIST;
    *ctx->alarm_set_field = ALARM_FIELD_STATUS;
    ESP_LOGI(settings_logic_log_tag(ctx), "取消鬧鐘編輯");
    ui_refresh();
}

void settings_logic_save_alarm_setting_and_exit(settings_logic_context_t *ctx)
{
    if (!settings_logic_context_valid(ctx) || *ctx->alarm_ui_mode != ALARM_UI_EDIT)
    {
        return;
    }

    int index = settings_logic_clamp_alarm_index(ctx, *ctx->selected_alarm_index);
    ctx->alarms[index] = *ctx->alarm_edit;
    *ctx->alarm_ui_mode = ALARM_UI_LIST;
    *ctx->alarm_set_field = ALARM_FIELD_STATUS;
    *ctx->current_panel = *ctx->last_clock_panel_before_overlay;
    alarm_save_all_to_nvs(ctx->alarms, ctx->alarm_count);

    ESP_LOGI(settings_logic_log_tag(ctx), "已儲存鬧鐘 A%d", index + 1);
    ui_refresh();
}

void settings_logic_adjust_alarm_field(settings_logic_context_t *ctx, int delta)
{
    if (!settings_logic_context_valid(ctx) || *ctx->alarm_ui_mode != ALARM_UI_EDIT)
    {
        return;
    }

    switch (*ctx->alarm_set_field)
    {
    case ALARM_FIELD_STATUS:
        settings_logic_cycle_alarm_status(ctx->alarm_edit, delta);
        ESP_LOGI(settings_logic_log_tag(ctx), "鬧鐘狀態: %s", settings_logic_alarm_status_text(ctx->alarm_edit));
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
    if (!settings_logic_context_valid(ctx) || *ctx->alarm_ui_mode != ALARM_UI_EDIT)
    {
        return;
    }

    switch (*ctx->alarm_set_field)
    {
    case ALARM_FIELD_STATUS:
        *ctx->alarm_set_field = ALARM_FIELD_HOUR;
        break;
    case ALARM_FIELD_HOUR:
        *ctx->alarm_set_field = ALARM_FIELD_MINUTE;
        break;
    case ALARM_FIELD_MINUTE:
    default:
        *ctx->alarm_set_field = ALARM_FIELD_STATUS;
        break;
    }

    ui_refresh();
}
