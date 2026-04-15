#include "alarm_logic.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "ALARM_LOGIC";

#define ALARM_NAMESPACE "alarm_cfg_v2"
#define ALARM_LEGACY_NAMESPACE "alarm_cfg"
#define ALARM_KEY_VERSION "version"
#define ALARM_KEY_SLOTS "slots"
#define ALARM_KEY_ENABLED "enabled"
#define ALARM_KEY_HOUR "hour"
#define ALARM_KEY_MINUTE "minute"
#define ALARM_KEY_REPEAT "repeat"
#define ALARM_STORAGE_VERSION 2

typedef struct
{
    bool enabled;
    int hour;
    int minute;
    int repeat;
} alarm_config_storage_t;

typedef struct
{
    uint8_t version;
    alarm_config_storage_t alarms[ALARM_SLOT_COUNT];
} alarm_storage_blob_t;

static void alarm_set_default(alarm_config_t *alarm)
{
    if (alarm == NULL)
    {
        return;
    }

    alarm->enabled = false;
    alarm->hour = 7;
    alarm->minute = 0;
    alarm->repeat = ALARM_REPEAT_DAILY;
}

static void alarm_set_defaults(alarm_config_t *alarms, int count)
{
    if (alarms == NULL || count <= 0)
    {
        return;
    }

    for (int i = 0; i < count; i++)
    {
        alarm_set_default(&alarms[i]);
    }
}

static bool alarm_slot_valid(const alarm_config_storage_t *alarm)
{
    return alarm != NULL &&
           alarm->hour >= 0 && alarm->hour <= 23 &&
           alarm->minute >= 0 && alarm->minute <= 59 &&
           (alarm->repeat == ALARM_REPEAT_ONCE || alarm->repeat == ALARM_REPEAT_DAILY);
}

static void alarm_copy_to_storage(alarm_config_storage_t *dst, const alarm_config_t *src)
{
    if (dst == NULL || src == NULL)
    {
        return;
    }

    dst->enabled = src->enabled;
    dst->hour = src->hour;
    dst->minute = src->minute;
    dst->repeat = (int)src->repeat;
}

static void alarm_copy_from_storage(alarm_config_t *dst, const alarm_config_storage_t *src)
{
    if (dst == NULL || src == NULL)
    {
        return;
    }

    if (!alarm_slot_valid(src))
    {
        alarm_set_default(dst);
        return;
    }

    dst->enabled = src->enabled;
    dst->hour = src->hour;
    dst->minute = src->minute;
    dst->repeat = (alarm_repeat_t)src->repeat;
}

static bool alarm_load_legacy_first_slot(alarm_config_t *alarm)
{
    nvs_handle_t nvs_handle;
    alarm_config_storage_t storage = {
        .enabled = false,
        .hour = 7,
        .minute = 0,
        .repeat = ALARM_REPEAT_DAILY,
    };

    esp_err_t err = nvs_open(ALARM_LEGACY_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
    {
        return false;
    }

    uint8_t enabled = storage.enabled ? 1 : 0;
    int32_t hour = storage.hour;
    int32_t minute = storage.minute;
    int32_t repeat = storage.repeat;

    esp_err_t e1 = nvs_get_u8(nvs_handle, ALARM_KEY_ENABLED, &enabled);
    esp_err_t e2 = nvs_get_i32(nvs_handle, ALARM_KEY_HOUR, &hour);
    esp_err_t e3 = nvs_get_i32(nvs_handle, ALARM_KEY_MINUTE, &minute);
    esp_err_t e4 = nvs_get_i32(nvs_handle, ALARM_KEY_REPEAT, &repeat);
    nvs_close(nvs_handle);

    if (e1 != ESP_OK && e2 != ESP_OK && e3 != ESP_OK && e4 != ESP_OK)
    {
        return false;
    }

    storage.enabled = (enabled == 1);
    storage.hour = (int)hour;
    storage.minute = (int)minute;
    storage.repeat = (int)repeat;
    alarm_copy_from_storage(alarm, &storage);
    return true;
}

const char *alarm_repeat_text(int repeat)
{
    return (repeat == ALARM_REPEAT_DAILY) ? "DAILY" : "ONCE";
}

bool alarm_save_all_to_nvs(const alarm_config_t *alarms, int count)
{
    if (alarms == NULL || count <= 0)
    {
        ESP_LOGE(TAG, "alarm_save_all_to_nvs: invalid args");
        return false;
    }

    alarm_storage_blob_t blob = {0};
    blob.version = ALARM_STORAGE_VERSION;

    int limit = (count < ALARM_SLOT_COUNT) ? count : ALARM_SLOT_COUNT;
    for (int i = 0; i < limit; i++)
    {
        alarm_copy_to_storage(&blob.alarms[i], &alarms[i]);
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ALARM_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "打開 alarm NVS 失敗: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_u8(nvs_handle, ALARM_KEY_VERSION, blob.version);
    if (err == ESP_OK)
    {
        err = nvs_set_blob(nvs_handle, ALARM_KEY_SLOTS, &blob, sizeof(blob));
    }
    if (err == ESP_OK)
    {
        err = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "儲存 alarm NVS 失敗: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "已儲存 %d 組鬧鐘設定", limit);
    return true;
}

void alarm_load_all_from_nvs(alarm_config_t *alarms, int count)
{
    if (alarms == NULL || count <= 0)
    {
        ESP_LOGE(TAG, "alarm_load_all_from_nvs: invalid args");
        return;
    }

    alarm_set_defaults(alarms, count);

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ALARM_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err == ESP_OK)
    {
        size_t required_size = 0;
        err = nvs_get_blob(nvs_handle, ALARM_KEY_SLOTS, NULL, &required_size);
        if (err == ESP_OK && required_size == sizeof(alarm_storage_blob_t))
        {
            alarm_storage_blob_t blob = {0};
            err = nvs_get_blob(nvs_handle, ALARM_KEY_SLOTS, &blob, &required_size);
            if (err == ESP_OK && blob.version == ALARM_STORAGE_VERSION)
            {
                int limit = (count < ALARM_SLOT_COUNT) ? count : ALARM_SLOT_COUNT;
                for (int i = 0; i < limit; i++)
                {
                    alarm_copy_from_storage(&alarms[i], &blob.alarms[i]);
                }
                nvs_close(nvs_handle);
                ESP_LOGI(TAG, "已載入 %d 組鬧鐘設定", limit);
                return;
            }
        }
        nvs_close(nvs_handle);
    }

    if (alarm_load_legacy_first_slot(&alarms[0]))
    {
        ESP_LOGI(TAG, "已從舊版單組鬧鐘資料遷移到 A1");
        alarm_save_all_to_nvs(alarms, count);
        return;
    }

    ESP_LOGI(TAG, "未找到鬧鐘資料，使用預設值");
}
