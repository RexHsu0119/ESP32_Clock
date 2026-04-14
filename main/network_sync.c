#include "network_sync.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"

#include "esp_err.h"
#include "esp_log.h"

#include "my_rtc.h"
#include "ui.h"
#include "weather.h"
#include "wifi.h"
#include "wifi_config.h"

static bool network_sync_context_valid(const network_sync_context_t *ctx)
{
    return ctx != NULL &&
           ctx->wifi_credentials_loaded != NULL &&
           ctx->wifi_ssid != NULL &&
           ctx->wifi_ssid_size > 0 &&
           ctx->wifi_password != NULL &&
           ctx->wifi_password_size > 0 &&
           ctx->time_syncing != NULL &&
           ctx->wifi_failed != NULL &&
           ctx->force_unknown_during_sync != NULL &&
           ctx->time_base_valid != NULL &&
           ctx->rtc_time_valid != NULL;
}

static const char *network_sync_log_tag(const network_sync_context_t *ctx)
{
    return (ctx != NULL && ctx->log_tag != NULL) ? ctx->log_tag : "NETSYNC";
}

static void network_sync_task(void *arg)
{
    network_sync_context_t *ctx = (network_sync_context_t *)arg;

    if (!network_sync_context_valid(ctx))
    {
        vTaskDelete(NULL);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    *ctx->time_syncing = true;
    *ctx->wifi_failed = false;

    ESP_LOGI(network_sync_log_tag(ctx), "背景開始進行 Wi-Fi / NTP / Weather 更新");

    if (wifi_connect(ctx->wifi_ssid, ctx->wifi_password))
    {
        ESP_LOGI(network_sync_log_tag(ctx), "WIFI 連接成功，正在同步時間...");
        rtc_sync_from_ntp();

        if (rtc_is_ntp_synced())
        {
            *ctx->time_base_valid = true;
            *ctx->rtc_time_valid = true;
        }

        vTaskDelay(pdMS_TO_TICKS(800));

        if (!weather_update_now())
        {
            ESP_LOGW(network_sync_log_tag(ctx), "天氣更新失敗");
        }
    }
    else
    {
        *ctx->wifi_failed = true;
        ESP_LOGW(network_sync_log_tag(ctx), "WIFI 連接失敗 (SSID=%s)", ctx->wifi_ssid);

        if (*ctx->rtc_time_valid)
        {
            *ctx->time_base_valid = true;
        }
    }

    *ctx->time_syncing = false;
    ui_refresh();

    ESP_LOGI(network_sync_log_tag(ctx), "背景網路更新流程結束");
    vTaskDelete(NULL);
}

bool network_sync_is_button_held_on_boot(uint32_t gpio_num,
                                         const char *name,
                                         uint32_t hold_ms,
                                         uint32_t sample_ms)
{
    gpio_config_t io_conf = {0};
    io_conf.pin_bit_mask = (1ULL << gpio_num);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK)
    {
        ESP_LOGW("NETSYNC", "設定開機偵測 %s 腳位失敗: %s", name, esp_err_to_name(ret));
        return false;
    }

    if (gpio_get_level(gpio_num) != 0)
    {
        return false;
    }

    ESP_LOGI("NETSYNC", "偵測到開機時 %s 已按下，確認是否達門檻...", name);

    uint32_t loops = (sample_ms == 0) ? 0 : (hold_ms / sample_ms);
    for (uint32_t i = 0; i < loops; i++)
    {
        if (gpio_get_level(gpio_num) != 0)
        {
            ESP_LOGI("NETSYNC", "%s 已放開，不觸發開機功能", name);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(sample_ms));
    }

    ESP_LOGI("NETSYNC", "開機按住 %s 達門檻", name);
    return true;
}

bool network_sync_load_wifi_credentials(network_sync_context_t *ctx)
{
    if (!network_sync_context_valid(ctx))
    {
        return false;
    }

    memset(ctx->wifi_ssid, 0, ctx->wifi_ssid_size);
    memset(ctx->wifi_password, 0, ctx->wifi_password_size);
    *ctx->wifi_credentials_loaded = false;

    if (wifi_config_load_credentials(ctx->wifi_ssid,
                                     ctx->wifi_ssid_size,
                                     ctx->wifi_password,
                                     ctx->wifi_password_size))
    {
        *ctx->wifi_credentials_loaded = true;
        ESP_LOGI(network_sync_log_tag(ctx), "已從 NVS 載入 Wi-Fi 設定: SSID=%s", ctx->wifi_ssid);
        return true;
    }

    ESP_LOGW(network_sync_log_tag(ctx), "尚未設定 Wi-Fi credentials，將進入配網模式");
    return false;
}

bool network_sync_start(network_sync_context_t *ctx, bool force_unknown_during_sync)
{
    if (!network_sync_context_valid(ctx))
    {
        return false;
    }

    if (!*ctx->wifi_credentials_loaded)
    {
        ESP_LOGW(network_sync_log_tag(ctx), "尚未載入 Wi-Fi credentials，無法啟動同步");
        return false;
    }

    if (*ctx->time_syncing)
    {
        ESP_LOGI(network_sync_log_tag(ctx), "目前正在同步中");
        return false;
    }

    if (!wifi_init())
    {
        *ctx->time_syncing = false;
        *ctx->wifi_failed = true;
        ESP_LOGW(network_sync_log_tag(ctx), "WIFI 初始化失敗");
        ui_refresh();
        return false;
    }

    *ctx->force_unknown_during_sync = force_unknown_during_sync;
    *ctx->time_syncing = true;
    *ctx->wifi_failed = false;
    ui_refresh();

    BaseType_t ret = xTaskCreatePinnedToCore(network_sync_task,
                                             "network_time_task",
                                             8192,
                                             ctx,
                                             5,
                                             NULL,
                                             1);
    if (ret != pdPASS)
    {
        *ctx->time_syncing = false;
        *ctx->wifi_failed = true;
        ESP_LOGE(network_sync_log_tag(ctx), "建立 network_time_task 失敗");
        ui_refresh();
        return false;
    }

    return true;
}