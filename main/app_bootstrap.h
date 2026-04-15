#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "lvgl.h"

#include "button.h"
#include "clock_types.h"
#include "network_sync.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        const char *log_tag;
        network_sync_context_t *network_sync;
        alarm_config_t *alarms;
        app_mode_t *app_mode;
        boot_hint_t *boot_hint;
        bool *time_base_valid;
        bool *rtc_time_valid;
        bool *force_unknown_during_sync;
        volatile bool *time_syncing;
        volatile bool *wifi_failed;
        int *calendar_year;
        int *calendar_month;
        SemaphoreHandle_t *lvgl_mutex;
        uint32_t portal_force_hold_ms;
        uint32_t portal_force_sample_ms;
        uint32_t boot_hint_force_setup_ms;
        uint32_t boot_hint_clear_wifi_ms;
        void (*update_ui)(void);
        button_callback_t button_callback;
        void (*lvgl_tick_cb)(void *arg);
        TaskFunction_t lvgl_task;
        void (*create_calendar_ui)(lv_obj_t *scr);
        void (*update_panel_visibility)(void);
    } app_bootstrap_context_t;

    bool app_bootstrap_run(app_bootstrap_context_t *ctx);

#ifdef __cplusplus
}
#endif