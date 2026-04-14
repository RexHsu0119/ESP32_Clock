#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        const char *log_tag;
        bool *wifi_credentials_loaded;
        char *wifi_ssid;
        size_t wifi_ssid_size;
        char *wifi_password;
        size_t wifi_password_size;
        volatile bool *time_syncing;
        volatile bool *wifi_failed;
        bool *force_unknown_during_sync;
        bool *time_base_valid;
        bool *rtc_time_valid;
    } network_sync_context_t;

    bool network_sync_is_button_held_on_boot(uint32_t gpio_num,
                                             const char *name,
                                             uint32_t hold_ms,
                                             uint32_t sample_ms);

    bool network_sync_load_wifi_credentials(network_sync_context_t *ctx);
    bool network_sync_start(network_sync_context_t *ctx, bool force_unknown_during_sync);

#ifdef __cplusplus
}
#endif