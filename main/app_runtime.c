#include "app_runtime.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "ui.h"
#include "wifi.h"
#include "wifi_config.h"
#include "wifi_portal.h"

static const char *app_runtime_log_tag(const app_runtime_context_t *ctx)
{
    return (ctx != NULL && ctx->log_tag != NULL) ? ctx->log_tag : "RUNTIME";
}

static bool app_runtime_context_valid(const app_runtime_context_t *ctx)
{
    return ctx != NULL &&
           ctx->network_sync != NULL &&
           ctx->request_deep_sleep != NULL &&
           ctx->request_open_wifi_setup != NULL &&
           ctx->request_clear_wifi != NULL &&
           ctx->wifi_ssid != NULL &&
           ctx->wifi_ssid_size > 0 &&
           ctx->wifi_password != NULL &&
           ctx->wifi_password_size > 0 &&
           ctx->wifi_credentials_loaded != NULL &&
           ctx->menu_open != NULL &&
           ctx->confirm_open != NULL &&
           ctx->confirm_action != NULL &&
           ctx->confirm_yes_selected != NULL &&
           ctx->app_mode != NULL &&
           ctx->current_panel != NULL &&
           ctx->wifi_failed != NULL &&
           ctx->time_syncing != NULL &&
           ctx->force_unknown_during_sync != NULL &&
           ctx->enter_deep_sleep != NULL;
}

static void app_runtime_stop_sd_player(app_runtime_context_t *ctx)
{
    if (ctx != NULL && ctx->stop_sd_player != NULL)
    {
        ctx->stop_sd_player();
    }
}

static void app_runtime_handle_clear_wifi(app_runtime_context_t *ctx)
{
    *ctx->request_clear_wifi = false;

    ESP_LOGI(app_runtime_log_tag(ctx), "由 Menu 清除 Wi-Fi 設定");

    if (wifi_portal_is_running())
    {
        wifi_portal_stop();
    }

    wifi_disconnect();

    if (!wifi_config_clear_credentials())
    {
        ESP_LOGW(app_runtime_log_tag(ctx), "由 Menu 清除 Wi-Fi 設定失敗");
    }

    memset(ctx->wifi_ssid, 0, ctx->wifi_ssid_size);
    memset(ctx->wifi_password, 0, ctx->wifi_password_size);
    *ctx->wifi_credentials_loaded = false;

    *ctx->menu_open = false;
    *ctx->confirm_open = false;
    *ctx->confirm_action = CONFIRM_NONE;
    *ctx->confirm_yes_selected = false;

    *ctx->app_mode = APP_MODE_WIFI_PORTAL;
    *ctx->current_panel = PANEL_DIGITAL;
    *ctx->wifi_failed = false;
    *ctx->time_syncing = false;
    *ctx->force_unknown_during_sync = false;

    if (!wifi_portal_start(NULL, NULL))
    {
        ESP_LOGE(app_runtime_log_tag(ctx), "清除 Wi-Fi 後啟動 portal 失敗");
    }

    ui_refresh();
}

static void app_runtime_handle_open_wifi_setup(app_runtime_context_t *ctx)
{
    *ctx->request_open_wifi_setup = false;

    if (*ctx->app_mode != APP_MODE_WIFI_PORTAL)
    {
        clock_panel_t restore_panel = *ctx->current_panel;

        ESP_LOGI(app_runtime_log_tag(ctx), "由 Menu 進入 Wi-Fi Setup");

        wifi_disconnect();
        *ctx->app_mode = APP_MODE_WIFI_PORTAL;

        if (!wifi_portal_is_running())
        {
            if (!wifi_portal_start(NULL, NULL))
            {
                ESP_LOGE(app_runtime_log_tag(ctx), "由 Menu 啟動 Wi-Fi portal 失敗");
                *ctx->app_mode = APP_MODE_CLOCK;
                *ctx->current_panel = restore_panel;
            }
        }

        ui_refresh();
    }
}

static void app_runtime_handle_new_credentials(app_runtime_context_t *ctx)
{
    ESP_LOGI(app_runtime_log_tag(ctx), "偵測到新的 Wi-Fi credentials，準備切回 STA 模式");

    ui_refresh();
    vTaskDelay(pdMS_TO_TICKS(ctx->portal_saved_status_ms));

    wifi_portal_clear_new_credentials_flag();

    wifi_portal_stop();
    wifi_disconnect();

    if (network_sync_load_wifi_credentials(ctx->network_sync))
    {
        *ctx->app_mode = APP_MODE_CLOCK;
        *ctx->current_panel = PANEL_DIGITAL;
        *ctx->wifi_failed = false;
        *ctx->time_syncing = false;
        *ctx->force_unknown_during_sync = true;

        ui_refresh();
        network_sync_start(ctx->network_sync, true);
    }
    else
    {
        ESP_LOGE(app_runtime_log_tag(ctx), "重新載入 Wi-Fi credentials 失敗，回到 portal 模式");
        *ctx->app_mode = APP_MODE_WIFI_PORTAL;
        wifi_portal_start(NULL, NULL);
        ui_refresh();
    }
}

void app_runtime_process(app_runtime_context_t *ctx)
{
    if (!app_runtime_context_valid(ctx))
    {
        return;
    }

    if (*ctx->request_deep_sleep)
    {
        *ctx->request_deep_sleep = false;
        app_runtime_stop_sd_player(ctx);
        ctx->enter_deep_sleep();
    }

    if (*ctx->request_clear_wifi)
    {
        app_runtime_handle_clear_wifi(ctx);
    }

    if (*ctx->request_open_wifi_setup)
    {
        app_runtime_handle_open_wifi_setup(ctx);
    }

    if (*ctx->app_mode == APP_MODE_WIFI_PORTAL && wifi_portal_has_new_credentials())
    {
        app_runtime_handle_new_credentials(ctx);
    }
}