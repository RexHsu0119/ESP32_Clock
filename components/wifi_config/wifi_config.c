#include "wifi_config.h"

#include <string.h>

#include "nvs.h"
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "WIFI_CFG";

#define WIFI_CFG_NAMESPACE "wifi_cfg"
#define WIFI_CFG_KEY_SSID "ssid"
#define WIFI_CFG_KEY_PASSWORD "password"
#define WIFI_CFG_KEY_VALID "valid"

static bool is_valid_input_string(const char *s, size_t max_len)
{
    if (s == NULL)
    {
        return false;
    }

    size_t len = strnlen(s, max_len + 1);
    if (len == 0 || len > max_len)
    {
        return false;
    }

    return true;
}

bool wifi_config_save_credentials(const char *ssid, const char *password)
{
    if (!is_valid_input_string(ssid, WIFI_CONFIG_SSID_MAX_LEN))
    {
        ESP_LOGE(TAG, "SSID 無效");
        return false;
    }

    if (password == NULL)
    {
        ESP_LOGE(TAG, "password 為 NULL");
        return false;
    }

    size_t password_len = strnlen(password, WIFI_CONFIG_PASSWORD_MAX_LEN + 1);
    if (password_len > WIFI_CONFIG_PASSWORD_MAX_LEN)
    {
        ESP_LOGE(TAG, "password 長度超過上限");
        return false;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_CFG_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "打開 NVS 失敗: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_str(nvs_handle, WIFI_CFG_KEY_SSID, ssid);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "儲存 SSID 失敗: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_set_str(nvs_handle, WIFI_CFG_KEY_PASSWORD, password);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "儲存 password 失敗: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_set_u8(nvs_handle, WIFI_CFG_KEY_VALID, 1);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "儲存 valid 旗標失敗: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "提交 NVS 失敗: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "Wi-Fi 帳密已儲存: SSID=%s", ssid);
    return true;
}

bool wifi_config_load_credentials(char *ssid,
                                  size_t ssid_len,
                                  char *password,
                                  size_t password_len)
{
    if (ssid == NULL || password == NULL || ssid_len == 0 || password_len == 0)
    {
        ESP_LOGE(TAG, "輸出 buffer 無效");
        return false;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_CFG_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
    {
        if (err == ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGW(TAG, "Wi-Fi 設定命名空間不存在");
        }
        else
        {
            ESP_LOGE(TAG, "打開 NVS 失敗: %s", esp_err_to_name(err));
        }
        return false;
    }

    uint8_t valid = 0;
    err = nvs_get_u8(nvs_handle, WIFI_CFG_KEY_VALID, &valid);
    if (err != ESP_OK || valid != 1)
    {
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGE(TAG, "讀取 valid 旗標失敗: %s", esp_err_to_name(err));
        }
        else
        {
            ESP_LOGW(TAG, "尚未儲存有效的 Wi-Fi 設定");
        }

        nvs_close(nvs_handle);
        return false;
    }

    size_t ssid_required_len = ssid_len;
    err = nvs_get_str(nvs_handle, WIFI_CFG_KEY_SSID, ssid, &ssid_required_len);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "讀取 SSID 失敗: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }

    size_t password_required_len = password_len;
    err = nvs_get_str(nvs_handle, WIFI_CFG_KEY_PASSWORD, password, &password_required_len);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "讀取 password 失敗: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "已載入 Wi-Fi 設定: SSID=%s", ssid);
    return true;
}

bool wifi_config_has_credentials(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_CFG_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
    {
        if (err != ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGE(TAG, "打開 NVS 失敗: %s", esp_err_to_name(err));
        }
        return false;
    }

    uint8_t valid = 0;
    err = nvs_get_u8(nvs_handle, WIFI_CFG_KEY_VALID, &valid);
    nvs_close(nvs_handle);

    if (err == ESP_OK && valid == 1)
    {
        return true;
    }

    return false;
}

bool wifi_config_clear_credentials(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_CFG_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "打開 NVS 失敗: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_erase_key(nvs_handle, WIFI_CFG_KEY_SSID);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGE(TAG, "清除 SSID 失敗: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_erase_key(nvs_handle, WIFI_CFG_KEY_PASSWORD);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGE(TAG, "清除 password 失敗: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_erase_key(nvs_handle, WIFI_CFG_KEY_VALID);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGE(TAG, "清除 valid 旗標失敗: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "提交 NVS 失敗: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "已清除 Wi-Fi 設定");
    return true;
}