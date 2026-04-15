#include "menu_logic.h"

#include "calendar_logic.h"
#include "ui.h"

static bool menu_logic_context_valid(const menu_logic_context_t *ctx)
{
    return ctx != NULL &&
           ctx->menu_open != NULL &&
           ctx->menu_selected != NULL &&
           ctx->menu_top_index != NULL &&
           ctx->confirm_open != NULL &&
           ctx->confirm_action != NULL &&
           ctx->confirm_yes_selected != NULL &&
           ctx->request_deep_sleep != NULL &&
           ctx->request_open_wifi_setup != NULL &&
           ctx->request_clear_wifi != NULL &&
           ctx->current_panel != NULL &&
           ctx->last_clock_panel_before_overlay != NULL &&
           ctx->calendar_adjust_field != NULL &&
           ctx->calendar_year != NULL &&
           ctx->calendar_month != NULL;
}

const char *menu_logic_item_text(menu_item_t item)
{
    static const char *texts[MENU_ITEM_COUNT] = {
        "Alarm",
        "Countdown Timer",
        "Set Time",
        "Calendar",
        "Wi-Fi Setup",
        "Clear Wi-Fi",
        "Sync Now",
        "Deep Sleep",
    };

    if ((int)item < 0 || (int)item >= MENU_ITEM_COUNT)
    {
        return "";
    }

    return texts[(int)item];
}

void menu_logic_open(menu_logic_context_t *ctx)
{
    if (!menu_logic_context_valid(ctx))
    {
        return;
    }

    *ctx->menu_open = true;

    if ((int)(*ctx->menu_selected) < 0 || (int)(*ctx->menu_selected) >= MENU_ITEM_COUNT)
    {
        *ctx->menu_selected = MENU_ITEM_ALARM;
    }

    if (*ctx->menu_top_index < 0)
    {
        *ctx->menu_top_index = 0;
    }

    if (*ctx->menu_top_index > (MENU_ITEM_COUNT - 1))
    {
        *ctx->menu_top_index = MENU_ITEM_COUNT - 1;
    }

    ui_refresh();
}

void menu_logic_close(menu_logic_context_t *ctx)
{
    if (!menu_logic_context_valid(ctx))
    {
        return;
    }

    *ctx->menu_open = false;
    ui_refresh();
}

void menu_logic_move(menu_logic_context_t *ctx, int delta)
{
    if (!menu_logic_context_valid(ctx))
    {
        return;
    }

    int sel = (int)(*ctx->menu_selected) + delta;

    if (sel < 0)
    {
        sel = 0;
    }
    if (sel >= MENU_ITEM_COUNT)
    {
        sel = MENU_ITEM_COUNT - 1;
    }

    *ctx->menu_selected = (menu_item_t)sel;

    if ((int)(*ctx->menu_selected) < *ctx->menu_top_index)
    {
        *ctx->menu_top_index = (int)(*ctx->menu_selected);
    }
    else if ((int)(*ctx->menu_selected) >= (*ctx->menu_top_index + 4))
    {
        *ctx->menu_top_index = (int)(*ctx->menu_selected) - 3;
    }

    ui_refresh();
}

void menu_logic_confirm_open(menu_logic_context_t *ctx, confirm_action_t action)
{
    if (!menu_logic_context_valid(ctx))
    {
        return;
    }

    *ctx->confirm_action = action;
    *ctx->confirm_yes_selected = false;
    *ctx->confirm_open = true;
    ui_refresh();
}

void menu_logic_confirm_close(menu_logic_context_t *ctx)
{
    if (!menu_logic_context_valid(ctx))
    {
        return;
    }

    *ctx->confirm_open = false;
    *ctx->confirm_action = CONFIRM_NONE;
    *ctx->confirm_yes_selected = false;
    ui_refresh();
}

void menu_logic_confirm_execute(menu_logic_context_t *ctx)
{
    if (!menu_logic_context_valid(ctx) || !*ctx->confirm_open)
    {
        return;
    }

    confirm_action_t action = *ctx->confirm_action;
    bool yes = *ctx->confirm_yes_selected;

    *ctx->confirm_open = false;
    *ctx->confirm_action = CONFIRM_NONE;
    *ctx->confirm_yes_selected = false;

    if (!yes)
    {
        ui_refresh();
        return;
    }

    switch (action)
    {
    case CONFIRM_CLEAR_WIFI:
        *ctx->request_clear_wifi = true;
        break;

    case CONFIRM_NONE:
    default:
        break;
    }

    ui_refresh();
}

void menu_logic_execute_selected(menu_logic_context_t *ctx)
{
    if (!menu_logic_context_valid(ctx))
    {
        return;
    }

    menu_item_t item = *ctx->menu_selected;
    *ctx->menu_open = false;

    switch (item)
    {
    case MENU_ITEM_ALARM:
        if (ctx->enter_alarm_setting_mode != NULL)
        {
            ctx->enter_alarm_setting_mode();
        }
        break;

    case MENU_ITEM_TIMER:
        if (ctx->enter_timer_mode != NULL)
        {
            ctx->enter_timer_mode();
        }
        break;

    case MENU_ITEM_SET_TIME:
        if (ctx->enter_time_setting_mode != NULL)
        {
            ctx->enter_time_setting_mode();
        }
        ui_refresh();
        break;

    case MENU_ITEM_CALENDAR:
        if (*ctx->current_panel != PANEL_CALENDAR)
        {
            *ctx->last_clock_panel_before_overlay = *ctx->current_panel;
        }
        *ctx->calendar_adjust_field = CALENDAR_ADJUST_MONTH;
        calendar_ensure_initialized(ctx->calendar_year, ctx->calendar_month);
        *ctx->current_panel = PANEL_CALENDAR;
        ui_refresh();
        break;

    case MENU_ITEM_WIFI_SETUP:
        *ctx->request_open_wifi_setup = true;
        ui_refresh();
        break;

    case MENU_ITEM_CLEAR_WIFI:
        menu_logic_confirm_open(ctx, CONFIRM_CLEAR_WIFI);
        break;

    case MENU_ITEM_SYNC_NOW:
        if (ctx->start_manual_resync != NULL)
        {
            ctx->start_manual_resync();
        }
        ui_refresh();
        break;

    case MENU_ITEM_DEEP_SLEEP:
        *ctx->request_deep_sleep = true;
        break;

    default:
        ui_refresh();
        break;
    }
}