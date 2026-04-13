#include "alarm_logic.h"

#include <stdint.h>
#include <stdbool.h>

#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "ALARM_LOGIC";

#define ALARM_NAMESPACE "alarm_cfg"
#define ALARM_KEY_ENABLED "enabled"
#define ALARM_KEY_HOUR "hour"
#define ALARM_KEY_MINUTE "minute"
#define ALARM_KEY_REPEAT "repeat"

/*
 * 這個 private struct 是為了對應您目前 main.c 裡的：
 *
 * typedef struct
 * {
 *     bool enabled;
 *     int hour;
 *     int minute;
 *     alarm_repeat_t repeat;
 * } alarm_config_t;
 *
 * 在目前 ESP-IDF / xtensa / gcc 下，enum 預設底層通常為 int，
 * 所以這裡用 int repeat 來對齊現況。
 */
typedef struct
{
    bool enabled;
    int hour;
    int minute;
    int repeat;
} alarm_config_storage_t;

const char *alarm_repeat_text(int repeat)
{
    /* 依您目前 main.c:
     * ALARM_REPEAT_ONCE  = 0
     * ALARM_REPEAT_DAILY = 1
     */
    return (repeat == 1) ? "DAILY" : "ONCE";
}

bool alarm_save_to_nvs(const void *alarm_cfg)
{
    if (alarm_cfg == NULL)
    {
        ESP_LOGE(TAG, "alarm_save_to_nvs: alarm_cfg is NULL");
        return false;
    }

    const alarm_config_storage_t *alarm = (const alarm_config_storage_t *)alarm_cfg;

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ALARM_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "打開 alarm NVS 失敗: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_u8(nvs_handle, ALARM_KEY_ENABLED, alarm->enabled ? 1 : 0);
    if (err == ESP_OK)
        err = nvs_set_i32(nvs_handle, ALARM_KEY_HOUR, alarm->hour);
    if (err == ESP_OK)
        err = nvs_set_i32(nvs_handle, ALARM_KEY_MINUTE, alarm->minute);
    if (err == ESP_OK)
        err = nvs_set_i32(nvs_handle, ALARM_KEY_REPEAT, (int32_t)alarm->repeat);
    if (err == ESP_OK)
        err = nvs_commit(nvs_handle);

    nvs_close(nvs_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "儲存 alarm NVS 失敗: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "鬧鐘已儲存: enabled=%d time=%02d:%02d repeat=%s",
             alarm->enabled,
             alarm->hour,
             alarm->minute,
             alarm_repeat_text(alarm->repeat));

    return true;
}

void alarm_load_from_nvs(void *alarm_cfg)
{
    if (alarm_cfg == NULL)
    {
        ESP_LOGE(TAG, "alarm_load_from_nvs: alarm_cfg is NULL");
        return;
    }

    alarm_config_storage_t *alarm = (alarm_config_storage_t *)alarm_cfg;

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ALARM_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
    {
        if (err != ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGE(TAG, "打開 alarm NVS 失敗: %s", esp_err_to_name(err));
        }
        return;
    }

    uint8_t enabled = alarm->enabled ? 1 : 0;
    int32_t hour = alarm->hour;
    int32_t minute = alarm->minute;
    int32_t repeat = alarm->repeat;

    esp_err_t e1 = nvs_get_u8(nvs_handle, ALARM_KEY_ENABLED, &enabled);
    esp_err_t e2 = nvs_get_i32(nvs_handle, ALARM_KEY_HOUR, &hour);
    esp_err_t e3 = nvs_get_i32(nvs_handle, ALARM_KEY_MINUTE, &minute);
    esp_err_t e4 = nvs_get_i32(nvs_handle, ALARM_KEY_REPEAT, &repeat);

    nvs_close(nvs_handle);

    if (e1 == ESP_OK)
        alarm->enabled = (enabled == 1);

    if (e2 == ESP_OK && hour >= 0 && hour <= 23)
        alarm->hour = (int)hour;

    if (e3 == ESP_OK && minute >= 0 && minute <= 59)
        alarm->minute = (int)minute;

    if (e4 == ESP_OK && (repeat == 0 || repeat == 1))
        alarm->repeat = (int)repeat;

    ESP_LOGI(TAG, "載入鬧鐘: enabled=%d time=%02d:%02d repeat=%s",
             alarm->enabled,
             alarm->hour,
             alarm->minute,
             alarm_repeat_text(alarm->repeat));
}