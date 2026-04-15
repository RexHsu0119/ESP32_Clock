#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "clock_types.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef void (*ui_refresh_cb_t)(void);
    typedef const char *(*ui_menu_item_text_cb_t)(int item);

    void ui_init(SemaphoreHandle_t mutex, ui_refresh_cb_t refresh_cb);

    bool ui_lock(TickType_t timeout_ticks);
    void ui_unlock(void);
    void ui_refresh(void);
    SemaphoreHandle_t ui_get_mutex(void);

    /* overlay / portal */
    void ui_create_boot_overlay(lv_obj_t *scr);
    void ui_show_boot_overlay(int hint);
    void ui_hide_boot_overlay(void);

    void ui_create_alarm_overlay(lv_obj_t *scr);
    void ui_alarm_overlay_move_foreground(void);
    void ui_update_alarm_overlay_locked(bool alarm_setting_mode,
                                        int alarm_ui_mode,
                                        int selected_alarm_index,
                                        const alarm_config_t *alarms,
                                        int alarm_count,
                                        int alarm_field,
                                        bool enabled,
                                        int repeat,
                                        int hour,
                                        int minute);

    void ui_create_timer_overlay(lv_obj_t *scr);
    void ui_update_timer_overlay_locked(bool timer_mode,
                                        int timer_state,
                                        int timer_field,
                                        int timer_hours,
                                        int timer_minutes,
                                        int timer_seconds,
                                        int remaining_seconds);

    void ui_create_menu_overlay(lv_obj_t *scr);
    void ui_update_menu_overlay_locked(bool menu_open,
                                       bool app_clock_mode,
                                       int selected,
                                       int top_index,
                                       int item_count,
                                       ui_menu_item_text_cb_t item_text_cb);

    void ui_create_confirm_overlay(lv_obj_t *scr);
    void ui_update_confirm_overlay_locked(bool confirm_open,
                                          bool app_clock_mode,
                                          int confirm_action,
                                          bool yes_selected);

    void ui_create_portal_ui(lv_obj_t *scr);
    void ui_set_portal_visible(bool visible);
    void ui_update_portal_ui(void);

#ifdef __cplusplus
}
#endif