#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
        volatile bool *request_deep_sleep;
        volatile bool *request_open_wifi_setup;
        volatile bool *request_clear_wifi;
        char *wifi_ssid;
        size_t wifi_ssid_size;
        char *wifi_password;
        size_t wifi_password_size;
        bool *wifi_credentials_loaded;
        bool *menu_open;
        bool *confirm_open;
        confirm_action_t *confirm_action;
        bool *confirm_yes_selected;
        app_mode_t *app_mode;
        clock_panel_t *current_panel;
        volatile bool *wifi_failed;
        volatile bool *time_syncing;
        bool *force_unknown_during_sync;
        uint32_t portal_saved_status_ms;
        void (*enter_deep_sleep)(void);
    } app_runtime_context_t;

    void app_runtime_process(app_runtime_context_t *ctx);

#ifdef __cplusplus
}
#endif