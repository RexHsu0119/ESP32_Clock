#include "wifi.h"

#include <string.h>
#include <stdlib.h>

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

static const char *TAG = "WIFI";

/* 事件組用於 WIFI 連接狀態 */
static EventGroupHandle_t wifi_event_group = NULL;
static const int WIFI_CONNECTED_BIT = BIT0;
static const int WIFI_FAIL_BIT = BIT1;

static int retry_num = 0;
static const int MAXIMUM_RETRY = 5;
static bool wifi_initialized = false;
static bool wifi_started = false;
static bool wifi_sta_autoconnect = false;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT)
    {
        if (event_id == WIFI_EVENT_STA_START)
        {
            if (wifi_sta_autoconnect)
            {
                ESP_LOGI(TAG, "WIFI 開始連接...");
                esp_wifi_connect();
            }
            else
            {
                ESP_LOGI(TAG, "STA_START，autoconnect 已停用");
            }
        }
        else if (event_id == WIFI_EVENT_STA_DISCONNECTED)
        {
            wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;

            xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);

            if (event != NULL)
            {
                ESP_LOGW(TAG, "WIFI 斷線，reason=%d", event->reason);
            }

            if (wifi_sta_autoconnect && retry_num < MAXIMUM_RETRY)
            {
                esp_wifi_connect();
                retry_num++;
                ESP_LOGI(TAG, "重試連接 WIFI... (%d/%d)", retry_num, MAXIMUM_RETRY);
            }
            else if (wifi_sta_autoconnect)
            {
                xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
                ESP_LOGW(TAG, "連接 WIFI 失敗，已達最大重試次數");
            }
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "成功獲取 IP 位址:" IPSTR, IP2STR(&event->ip_info.ip));

        /* 關閉 Wi-Fi Power Save，減少 NTP/UDP 封包延遲 */
        esp_err_t ret = esp_wifi_set_ps(WIFI_PS_NONE);
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "關閉 Wi-Fi Power Save 失敗: %s", esp_err_to_name(ret));
        }

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

    ret = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "設置 WIFI storage 失敗: %s", esp_err_to_name(ret));
        return false;
    }

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
    wifi_sta_autoconnect = false;
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
    wifi_sta_autoconnect = true;

    wifi_config_t wifi_config = {0};

    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    if (strlen(password) == 0)
    {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
    else
    {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_WPA3_PSK;
    }

    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

#if CONFIG_ESP_WIFI_ENABLE_WPA3_SAE
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
#endif

    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "切換 WIFI 到 STA 模式失敗: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "設置 WIFI 配置失敗: %s", esp_err_to_name(ret));
        return false;
    }

    if (!wifi_started)
    {
        ret = esp_wifi_start();
        if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN)
        {
            ESP_LOGE(TAG, "啟動 WIFI 失敗: %s", esp_err_to_name(ret));
            return false;
        }
        wifi_started = true;
    }
    else
    {
        ret = esp_wifi_disconnect();
        if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_CONNECT)
        {
            ESP_LOGW(TAG, "中斷舊連線失敗: %s", esp_err_to_name(ret));
        }

        ret = esp_wifi_connect();
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "重新發起 WIFI 連接失敗: %s", esp_err_to_name(ret));
            return false;
        }
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

bool wifi_scan_networks(wifi_scan_result_t *results, uint16_t *count, uint16_t max_count)
{
    if (!wifi_initialized)
    {
        ESP_LOGE(TAG, "WIFI 未初始化");
        return false;
    }

    if (results == NULL || count == NULL || max_count == 0)
    {
        ESP_LOGE(TAG, "wifi_scan_networks 參數無效");
        return false;
    }

    uint16_t requested = max_count;
    if (requested > WIFI_SCAN_MAX_APS)
    {
        requested = WIFI_SCAN_MAX_APS;
    }

    wifi_ap_record_t *ap_records = calloc(requested, sizeof(wifi_ap_record_t));
    if (ap_records == NULL)
    {
        ESP_LOGE(TAG, "配置掃描記憶體失敗");
        return false;
    }

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };

    ESP_LOGI(TAG, "開始掃描附近 Wi-Fi AP...");

    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "啟動 Wi-Fi 掃描失敗: %s", esp_err_to_name(ret));
        free(ap_records);
        return false;
    }

    uint16_t ap_count = requested;
    ret = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "取得掃描結果失敗: %s", esp_err_to_name(ret));
        free(ap_records);
        return false;
    }

    uint16_t out_count = 0;
    for (uint16_t i = 0; i < ap_count && out_count < requested; i++)
    {
        if (strlen((const char *)ap_records[i].ssid) == 0)
        {
            continue;
        }

        /* 去除重複 SSID，只保留第一筆 */
        bool duplicated = false;
        for (uint16_t j = 0; j < out_count; j++)
        {
            if (strcmp(results[j].ssid, (const char *)ap_records[i].ssid) == 0)
            {
                duplicated = true;
                break;
            }
        }

        if (duplicated)
        {
            continue;
        }

        strncpy(results[out_count].ssid,
                (const char *)ap_records[i].ssid,
                sizeof(results[out_count].ssid) - 1);
        results[out_count].ssid[sizeof(results[out_count].ssid) - 1] = '\0';
        results[out_count].rssi = ap_records[i].rssi;
        results[out_count].authmode = ap_records[i].authmode;
        out_count++;
    }

    *count = out_count;

    ESP_LOGI(TAG, "Wi-Fi 掃描完成，共 %u 筆", out_count);

    free(ap_records);
    return true;
}

void wifi_disconnect(void)
{
    if (wifi_initialized)
    {
        wifi_sta_autoconnect = false;
        esp_wifi_disconnect();

        if (wifi_started)
        {
            esp_wifi_stop();
            wifi_started = false;
        }

        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
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