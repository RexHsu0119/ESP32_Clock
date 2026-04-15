#include "app_bootstrap.h"

#include "nvs_flash.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"

#include "alarm_logic.h"
#include "calendar_logic.h"
#include "display.h"
#include "my_rtc.h"
#include "ui.h"
#include "ui_clock.h"
#include "weather.h"
#include "wifi_config.h"
#include "wifi_portal.h"

static const char *app_bootstrap_log_tag(const app_bootstrap_context_t *ctx)
{
    return (ctx != NULL && ctx->log_tag != NULL) ? ctx->log_tag : "BOOT";
}

static bool app_bootstrap_context_valid(const app_bootstrap_context_t *ctx)
{
    return ctx != NULL &&
           ctx->network_sync != NULL &&
           ctx->alarms != NULL &&
           ctx->app_mode != NULL &&
           ctx->boot_hint != NULL &&
           ctx->time_base_valid != NULL &&
           ctx->rtc_time_valid != NULL &&
           ctx->force_unknown_during_sync != NULL &&
           ctx->time_syncing != NULL &&
           ctx->wifi_failed != NULL &&
           ctx->calendar_year != NULL &&
           ctx->calendar_month != NULL &&
           ctx->lvgl_mutex != NULL &&
           ctx->update_ui != NULL &&
           ctx->button_callback != NULL &&
           ctx->lvgl_tick_cb != NULL &&
           ctx->lvgl_task != NULL &&
           ctx->create_calendar_ui != NULL &&
           ctx->update_panel_visibility != NULL;
}

static void app_bootstrap_init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

static bool app_bootstrap_detect_wakeup(void)
{
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();

    ESP_LOGI("BOOT", "========================================");
    ESP_LOGI("BOOT", "ESP32-S3 時鐘顯示系統啟動");
    ESP_LOGI("BOOT", "========================================");
    ESP_LOGI("BOOT", "喚醒原因: %d", (int)wakeup_cause);

    return wakeup_cause == ESP_SLEEP_WAKEUP_EXT0;
}

static void app_bootstrap_resolve_boot_mode(app_bootstrap_context_t *ctx)
{
    bool has_wifi_credentials;
    bool clear_wifi_credentials;
    bool force_portal;

    clear_wifi_credentials = network_sync_is_button_held_on_boot(BUTTON_DOWN,
                                                                 "DOWN",
                                                                 ctx->portal_force_hold_ms,
                                                                 ctx->portal_force_sample_ms);
    force_portal = (!clear_wifi_credentials)
                       ? network_sync_is_button_held_on_boot(BUTTON_UP,
                                                             "UP",
                                                             ctx->portal_force_hold_ms,
                                                             ctx->portal_force_sample_ms)
                       : false;

    if (clear_wifi_credentials)
    {
        *ctx->boot_hint = BOOT_HINT_CLEAR_WIFI;
        ESP_LOGI(app_bootstrap_log_tag(ctx), "開機按住 DOWN：清除 Wi-Fi 設定並進入 ClockSetup");
        if (!wifi_config_clear_credentials())
        {
            ESP_LOGW(app_bootstrap_log_tag(ctx), "清除 Wi-Fi 設定失敗");
        }
    }
    else if (force_portal)
    {
        *ctx->boot_hint = BOOT_HINT_FORCE_SETUP;
    }
    else
    {
        *ctx->boot_hint = BOOT_HINT_NONE;
    }

    has_wifi_credentials = network_sync_load_wifi_credentials(ctx->network_sync);

    if (force_portal || clear_wifi_credentials)
    {
        *ctx->app_mode = APP_MODE_WIFI_PORTAL;
    }
    else
    {
        *ctx->app_mode = has_wifi_credentials ? APP_MODE_CLOCK : APP_MODE_WIFI_PORTAL;
    }
}

static bool app_bootstrap_init_runtime(app_bootstrap_context_t *ctx,
                                       bool woke_from_deep_sleep)
{
    if (woke_from_deep_sleep)
    {
        display_resume_from_sleep();
    }

    ESP_LOGI(app_bootstrap_log_tag(ctx), "初始化顯示模組...");
    display_init();

    *ctx->lvgl_mutex = xSemaphoreCreateMutex();
    if (*ctx->lvgl_mutex == NULL)
    {
        ESP_LOGE(app_bootstrap_log_tag(ctx), "建立 LVGL mutex 失敗");
        return false;
    }

    ui_init(*ctx->lvgl_mutex, ctx->update_ui);

    ESP_LOGI(app_bootstrap_log_tag(ctx), "初始化按鈕模組...");
    button_init();
    button_register_callback(ctx->button_callback);

    ESP_LOGI(app_bootstrap_log_tag(ctx), "初始化 RTC 模組...");
    my_rtc_init();

    ESP_LOGI(app_bootstrap_log_tag(ctx), "初始化 Weather 模組...");
    weather_init();

    alarm_load_all_from_nvs(ctx->alarms, ALARM_SLOT_COUNT);
    return true;
}

static void app_bootstrap_init_time_base(app_bootstrap_context_t *ctx,
                                         bool woke_from_deep_sleep)
{
    if (woke_from_deep_sleep && *ctx->rtc_time_valid)
    {
        *ctx->time_base_valid = true;
        ESP_LOGI(app_bootstrap_log_tag(ctx), "從 Deep Sleep 喚醒，沿用 RTC 系統時間");
    }
    else
    {
        *ctx->time_base_valid = false;
        ESP_LOGI(app_bootstrap_log_tag(ctx), "從 NVS 載入上次的時間...");
        rtc_load_from_nvs();
    }
}

static void app_bootstrap_start_lvgl_tick(app_bootstrap_context_t *ctx)
{
    const esp_timer_create_args_t tick_timer_args = {
        .callback = ctx->lvgl_tick_cb,
        .name = "lvgl_tick"};
    esp_timer_handle_t tick_timer;

    ESP_ERROR_CHECK(esp_timer_create(&tick_timer_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 2 * 1000));
}

static void app_bootstrap_create_ui(app_bootstrap_context_t *ctx)
{
    if (ui_lock(pdMS_TO_TICKS(100)))
    {
        lv_obj_t *scr = lv_screen_active();
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

        ui_clock_create_digital(scr);
        ui_clock_create_analog(scr);
        ctx->create_calendar_ui(scr);
        ui_create_portal_ui(scr);

        ui_create_boot_overlay(scr);
        ui_create_alarm_overlay(scr);
        ui_create_menu_overlay(scr);
        ui_create_confirm_overlay(scr);

        ctx->update_panel_visibility();

        if (*ctx->boot_hint != BOOT_HINT_NONE)
        {
            ui_show_boot_overlay((int)*ctx->boot_hint);
        }

        ui_unlock();
    }
}

static void app_bootstrap_init_sync_flags(app_bootstrap_context_t *ctx,
                                          bool woke_from_deep_sleep)
{
    *ctx->time_syncing = false;
    *ctx->wifi_failed = false;

    if (*ctx->app_mode == APP_MODE_WIFI_PORTAL)
    {
        *ctx->force_unknown_during_sync = false;
    }
    else if (woke_from_deep_sleep && *ctx->rtc_time_valid)
    {
        *ctx->force_unknown_during_sync = false;
    }
    else
    {
        *ctx->force_unknown_during_sync = true;
    }
}

static void app_bootstrap_start_ui_task(app_bootstrap_context_t *ctx,
                                        bool woke_from_deep_sleep)
{
    calendar_ensure_initialized(ctx->calendar_year, ctx->calendar_month);
    ui_refresh();

    ESP_LOGI(app_bootstrap_log_tag(ctx), "進入主迴圈");

    if (woke_from_deep_sleep)
    {
        vTaskDelay(pdMS_TO_TICKS(80));
    }
    else
    {
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    display_wake();
    xTaskCreatePinnedToCore(ctx->lvgl_task, "lvgl", 8192, NULL, 5, NULL, 1);
}

static void app_bootstrap_handle_boot_hint(app_bootstrap_context_t *ctx)
{
    uint32_t hint_delay_ms;

    if (*ctx->boot_hint == BOOT_HINT_NONE)
    {
        return;
    }

    hint_delay_ms = (*ctx->boot_hint == BOOT_HINT_CLEAR_WIFI)
                        ? ctx->boot_hint_clear_wifi_ms
                        : ctx->boot_hint_force_setup_ms;

    vTaskDelay(pdMS_TO_TICKS(hint_delay_ms));

    if (ui_lock(pdMS_TO_TICKS(100)))
    {
        ui_hide_boot_overlay();
        ui_unlock();
    }

    ui_refresh();
}

static void app_bootstrap_start_initial_mode(app_bootstrap_context_t *ctx,
                                             bool woke_from_deep_sleep)
{
    if (*ctx->app_mode == APP_MODE_WIFI_PORTAL)
    {
        if (!wifi_portal_start(NULL, NULL))
        {
            ESP_LOGE(app_bootstrap_log_tag(ctx), "啟動 Wi-Fi portal 失敗");
        }
        ui_refresh();
    }
    else
    {
        network_sync_start(ctx->network_sync,
                           !(woke_from_deep_sleep && *ctx->rtc_time_valid));
    }
}

bool app_bootstrap_run(app_bootstrap_context_t *ctx)
{
    bool woke_from_deep_sleep;

    if (!app_bootstrap_context_valid(ctx))
    {
        return false;
    }

    app_bootstrap_init_nvs();
    woke_from_deep_sleep = app_bootstrap_detect_wakeup();
    app_bootstrap_resolve_boot_mode(ctx);

    if (!app_bootstrap_init_runtime(ctx, woke_from_deep_sleep))
    {
        return false;
    }

    app_bootstrap_init_time_base(ctx, woke_from_deep_sleep);
    app_bootstrap_start_lvgl_tick(ctx);
    app_bootstrap_create_ui(ctx);
    app_bootstrap_init_sync_flags(ctx, woke_from_deep_sleep);
    app_bootstrap_start_ui_task(ctx, woke_from_deep_sleep);
    app_bootstrap_handle_boot_hint(ctx);
    app_bootstrap_start_initial_mode(ctx, woke_from_deep_sleep);

    return true;
}