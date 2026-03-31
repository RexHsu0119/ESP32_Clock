#include "wifi.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "WIFI";

// 事件組用於 WIFI 連接狀態
static EventGroupHandle_t wifi_event_group = NULL;
static const int WIFI_CONNECTED_BIT = BIT0;
static const int WIFI_FAIL_BIT = BIT1;

static int retry_num = 0;
static const int MAXIMUM_RETRY = 5;
static bool wifi_initialized = false;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT)
    {
        if (event_id == WIFI_EVENT_STA_START)
        {
            ESP_LOGI(TAG, "WIFI 開始連接...");
            esp_wifi_connect();
        }
        else if (event_id == WIFI_EVENT_STA_DISCONNECTED)
        {
            xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);

            if (retry_num < MAXIMUM_RETRY)
            {
                esp_wifi_connect();
                retry_num++;
                ESP_LOGI(TAG, "重試連接 WIFI... (%d/%d)", retry_num, MAXIMUM_RETRY);
            }
            else
            {
                xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
                ESP_LOGI(TAG, "連接 WIFI 失敗");
            }
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "成功獲取 IP 位址:" IPSTR, IP2STR(&event->ip_info.ip));

        /* 關閉 Wi-Fi Power Save，減少 NTP/UDP 封包延遲 */
        esp_wifi_set_ps(WIFI_PS_NONE);

        retry_num = 0;
        xEventGroupClearBits(wifi_event_group, WIFI_FAIL_BIT);
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

bool wifi_init(void)
{
    if (wifi_initialized)
    {
        ESP_LOGW(TAG, "WIFI 已初始化");
        return true;
    }

    wifi_event_group = xEventGroupCreate();
    if (wifi_event_group == NULL)
    {
        ESP_LOGE(TAG, "建立 WIFI 事件組失敗");
        return false;
    }

    ESP_ERROR_CHECK(esp_netif_init());

    esp_err_t ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "創建事件迴圈失敗: %s", esp_err_to_name(ret));
        return false;
    }

    if (esp_netif_create_default_wifi_sta() == NULL)
    {
        ESP_LOGE(TAG, "建立預設 WIFI STA 介面失敗");
        return false;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "註冊 WIFI 事件失敗: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "註冊 IP 事件失敗: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "設置 WIFI 模式失敗: %s", esp_err_to_name(ret));
        return false;
    }

    wifi_initialized = true;
    ESP_LOGI(TAG, "WIFI 初始化成功");
    return true;
}

bool wifi_connect(const char *ssid, const char *password)
{
    if (!wifi_initialized)
    {
        ESP_LOGE(TAG, "WIFI 未初始化");
        return false;
    }

    if (ssid == NULL || password == NULL)
    {
        ESP_LOGE(TAG, "SSID 或密碼為空");
        return false;
    }

    xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    retry_num = 0;

    wifi_config_t wifi_config = {0};

    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    if (strlen(password) == 0)
    {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
    else
    {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }

    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "設置 WIFI 配置失敗: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN)
    {
        ESP_LOGE(TAG, "啟動 WIFI 失敗: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "正在連接到 WIFI: %s", ssid);

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(20000));

    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "WIFI 連接成功");
        return true;
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGW(TAG, "WIFI 連接失敗");
        return false;
    }
    else
    {
        ESP_LOGW(TAG, "WIFI 連接超時");
        return false;
    }
}

void wifi_disconnect(void)
{
    if (wifi_initialized)
    {
        esp_wifi_disconnect();
        esp_wifi_stop();
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "WIFI 已斷開");
    }
}

bool wifi_is_connected(void)
{
    if (wifi_event_group == NULL)
    {
        return false;
    }

    EventBits_t bits = xEventGroupGetBits(wifi_event_group);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}