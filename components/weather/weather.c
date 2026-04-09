#include "weather.h"

#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

#include "wifi.h"

static const char *TAG = "WEATHER";

/*
 * 主來源：Open-Meteo
 * 備援來源：wttr.in（精簡格式）
 *
 * wttr.in 格式：
 *   %t = temperature
 *   %h = humidity
 *
 * 由於 query string 裡的 '%' 要做 URL encode，因此要寫成 %25
 * 回傳內容範例：
 *   +28°C;92%
 *   29°C;88%
 */
#define WEATHER_PRIMARY_URL \
    "https://api.open-meteo.com/v1/forecast?latitude=24.903&longitude=121.044&current=temperature_2m,relative_humidity_2m&timezone=Asia%2FTaipei"

#define WEATHER_BACKUP_URL \
    "https://wttr.in/24.903,121.044?format=%25t%3B%25h"

#define WEATHER_HTTP_TIMEOUT_MS 15000
#define WEATHER_HTTP_RX_BUF_SIZE 4096
#define WEATHER_HTTP_TX_BUF_SIZE 1024
#define WEATHER_RESPONSE_BUF_SIZE 4096

#define WEATHER_PRIMARY_RETRY_COUNT 2
#define WEATHER_BACKUP_RETRY_COUNT 2

typedef struct
{
    char *buffer;
    size_t capacity;
    size_t length;
    bool overflow;
} http_resp_ctx_t;

typedef bool (*weather_parser_fn_t)(const char *payload, float *temp_c, int *humidity_percent);

typedef struct
{
    const char *name;
    const char *url;
    const char *accept_header;
    weather_parser_fn_t parser;
    int retry_count;
    int retry_delay_ms;
} weather_provider_t;

static SemaphoreHandle_t s_weather_mutex = NULL;
static weather_info_t s_latest_weather = {
    .valid = false,
    .temperature_c = 0.0f,
    .humidity_percent = 0,
};

static esp_err_t weather_http_event_handler(esp_http_client_event_t *evt)
{
    http_resp_ctx_t *ctx = (http_resp_ctx_t *)evt->user_data;
    if (ctx == NULL)
    {
        return ESP_OK;
    }

    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        if (evt->data != NULL && evt->data_len > 0)
        {
            if (ctx->length + evt->data_len >= ctx->capacity)
            {
                ctx->overflow = true;
            }
            else
            {
                memcpy(ctx->buffer + ctx->length, evt->data, evt->data_len);
                ctx->length += evt->data_len;
                ctx->buffer[ctx->length] = '\0';
            }
        }
        break;

    default:
        break;
    }

    return ESP_OK;
}

static bool json_get_number_flexible(cJSON *obj, const char *key, double *out_value)
{
    if (obj == NULL || key == NULL || out_value == NULL)
    {
        return false;
    }

    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (item == NULL)
    {
        return false;
    }

    if (cJSON_IsNumber(item))
    {
        *out_value = item->valuedouble;
        return true;
    }

    if (cJSON_IsString(item) && item->valuestring != NULL)
    {
        char *endptr = NULL;
        double v = strtod(item->valuestring, &endptr);
        if (endptr != item->valuestring)
        {
            *out_value = v;
            return true;
        }
    }

    return false;
}

static bool fetch_weather_payload(const weather_provider_t *provider, char **out_payload)
{
    if (provider == NULL || out_payload == NULL)
    {
        return false;
    }

    *out_payload = NULL;

    char *response_buffer = calloc(1, WEATHER_RESPONSE_BUF_SIZE);
    if (response_buffer == NULL)
    {
        ESP_LOGE(TAG, "[%s] 配置回應 buffer 失敗", provider->name);
        return false;
    }

    http_resp_ctx_t ctx = {
        .buffer = response_buffer,
        .capacity = WEATHER_RESPONSE_BUF_SIZE,
        .length = 0,
        .overflow = false,
    };

    esp_http_client_config_t config = {
        .url = provider->url,
        .event_handler = weather_http_event_handler,
        .user_data = &ctx,
        .timeout_ms = WEATHER_HTTP_TIMEOUT_MS,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = WEATHER_HTTP_RX_BUF_SIZE,
        .buffer_size_tx = WEATHER_HTTP_TX_BUF_SIZE,
        .user_agent = "ESP32-Clock/1.0",
        .keep_alive_enable = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL)
    {
        ESP_LOGE(TAG, "[%s] 建立 HTTP client 失敗", provider->name);
        free(response_buffer);
        return false;
    }

    esp_http_client_set_header(client, "Accept", provider->accept_header ? provider->accept_header : "*/*");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    esp_http_client_set_header(client, "Connection", "close");

    esp_err_t ret = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    int content_length = esp_http_client_get_content_length(client);

    esp_http_client_cleanup(client);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "[%s] HTTP 請求失敗: %s", provider->name, esp_err_to_name(ret));
        free(response_buffer);
        return false;
    }

    if (status_code != 200)
    {
        ESP_LOGE(TAG, "[%s] HTTP 狀態碼錯誤: %d", provider->name, status_code);
        if (ctx.length > 0)
        {
            ESP_LOGW(TAG, "[%s] HTTP error body: %.200s", provider->name, response_buffer);
        }
        free(response_buffer);
        return false;
    }

    if (ctx.overflow)
    {
        ESP_LOGE(TAG, "[%s] 回應資料過大，buffer 不足", provider->name);
        free(response_buffer);
        return false;
    }

    if (ctx.length == 0)
    {
        ESP_LOGE(TAG, "[%s] 回應為空", provider->name);
        free(response_buffer);
        return false;
    }

    ESP_LOGD(TAG, "[%s] HTTP 200, content_length=%d, received=%u",
             provider->name, content_length, (unsigned)ctx.length);

    *out_payload = response_buffer;
    return true;
}

static bool parse_open_meteo_json(const char *json, float *temp_c, int *humidity_percent)
{
    if (json == NULL || temp_c == NULL || humidity_percent == NULL)
    {
        return false;
    }

    cJSON *root = cJSON_Parse(json);
    if (root == NULL)
    {
        ESP_LOGE(TAG, "[Open-Meteo] JSON 解析失敗");
        return false;
    }

    cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
    if (!cJSON_IsObject(current))
    {
        ESP_LOGE(TAG, "[Open-Meteo] 缺少 current 欄位");
        cJSON_Delete(root);
        return false;
    }

    double temp = 0.0;
    double hum = 0.0;

    bool ok_temp = json_get_number_flexible(current, "temperature_2m", &temp);
    bool ok_hum = json_get_number_flexible(current, "relative_humidity_2m", &hum);

    if (!ok_temp || !ok_hum)
    {
        ESP_LOGE(TAG, "[Open-Meteo] JSON 欄位不完整");
        cJSON_Delete(root);
        return false;
    }

    *temp_c = (float)temp;
    *humidity_percent = (int)(hum + 0.5);

    cJSON_Delete(root);
    return true;
}

static bool parse_wttr_text(const char *text, float *temp_c, int *humidity_percent)
{
    if (text == NULL || temp_c == NULL || humidity_percent == NULL)
    {
        return false;
    }

    const char *sep = strchr(text, ';');
    if (sep == NULL)
    {
        ESP_LOGE(TAG, "[wttr.in] 找不到分隔符 ';'，原始內容: %.100s", text);
        return false;
    }

    char temp_part[32] = {0};
    char hum_part[32] = {0};

    size_t temp_len = (size_t)(sep - text);
    if (temp_len >= sizeof(temp_part))
    {
        temp_len = sizeof(temp_part) - 1;
    }

    memcpy(temp_part, text, temp_len);
    temp_part[temp_len] = '\0';

    strncpy(hum_part, sep + 1, sizeof(hum_part) - 1);
    hum_part[sizeof(hum_part) - 1] = '\0';

    char temp_num[16] = {0};
    size_t ti = 0;
    for (size_t i = 0; temp_part[i] != '\0' && ti < sizeof(temp_num) - 1; i++)
    {
        char c = temp_part[i];
        if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.')
        {
            temp_num[ti++] = c;
        }
    }
    temp_num[ti] = '\0';

    char hum_num[16] = {0};
    size_t hi = 0;
    for (size_t i = 0; hum_part[i] != '\0' && hi < sizeof(hum_num) - 1; i++)
    {
        char c = hum_part[i];
        if (c >= '0' && c <= '9')
        {
            hum_num[hi++] = c;
        }
    }
    hum_num[hi] = '\0';

    if (strlen(temp_num) == 0 || strlen(hum_num) == 0)
    {
        ESP_LOGE(TAG, "[wttr.in] 無法解析 temp/humidity，原始內容: %.100s", text);
        return false;
    }

    *temp_c = (float)atof(temp_num);
    *humidity_percent = atoi(hum_num);

    return true;
}

static bool update_latest_weather(float temp_c, int humidity_percent, const char *provider_name)
{
    if (xSemaphoreTake(s_weather_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        s_latest_weather.valid = true;
        s_latest_weather.temperature_c = temp_c;
        s_latest_weather.humidity_percent = humidity_percent;
        xSemaphoreGive(s_weather_mutex);

        ESP_LOGI(TAG, "[%s] 湖口天氣更新成功: %.1f C, %d%%RH",
                 provider_name, temp_c, humidity_percent);
        return true;
    }

    ESP_LOGW(TAG, "[%s] weather mutex 取得失敗，略過寫入最新天氣", provider_name);
    return false;
}

static bool weather_update_from_provider(const weather_provider_t *provider)
{
    if (provider == NULL)
    {
        return false;
    }

    char *payload = NULL;
    if (!fetch_weather_payload(provider, &payload))
    {
        return false;
    }

    float temp_c = 0.0f;
    int humidity_percent = 0;

    bool ok = provider->parser(payload, &temp_c, &humidity_percent);
    if (!ok)
    {
        ESP_LOGW(TAG, "[%s] 資料解析失敗，原始回應前 200 bytes: %.200s",
                 provider->name, payload);
        free(payload);
        return false;
    }

    free(payload);
    return update_latest_weather(temp_c, humidity_percent, provider->name);
}

void weather_init(void)
{
    if (s_weather_mutex == NULL)
    {
        s_weather_mutex = xSemaphoreCreateMutex();
        if (s_weather_mutex == NULL)
        {
            ESP_LOGE(TAG, "建立 weather mutex 失敗");
        }
    }
}

bool weather_update_now(void)
{
    weather_init();

    if (s_weather_mutex == NULL)
    {
        return false;
    }

    if (!wifi_is_connected())
    {
        ESP_LOGW(TAG, "Wi-Fi 尚未連線，無法更新天氣");
        return false;
    }

    const weather_provider_t providers[] = {
        {
            .name = "Open-Meteo",
            .url = WEATHER_PRIMARY_URL,
            .accept_header = "application/json",
            .parser = parse_open_meteo_json,
            .retry_count = WEATHER_PRIMARY_RETRY_COUNT,
            .retry_delay_ms = 3000,
        },
        {
            .name = "wttr.in",
            .url = WEATHER_BACKUP_URL,
            .accept_header = "text/plain",
            .parser = parse_wttr_text,
            .retry_count = WEATHER_BACKUP_RETRY_COUNT,
            .retry_delay_ms = 5000,
        },
    };

    const int provider_count = sizeof(providers) / sizeof(providers[0]);

    for (int p = 0; p < provider_count; p++)
    {
        const weather_provider_t *provider = &providers[p];

        ESP_LOGI(TAG, "嘗試天氣來源: %s", provider->name);

        for (int attempt = 1; attempt <= provider->retry_count; attempt++)
        {
            if (weather_update_from_provider(provider))
            {
                return true;
            }

            if (attempt < provider->retry_count)
            {
                ESP_LOGW(TAG, "[%s] 更新失敗，準備重試... (%d/%d), delay=%d ms",
                         provider->name,
                         attempt,
                         provider->retry_count,
                         provider->retry_delay_ms);
                vTaskDelay(pdMS_TO_TICKS(provider->retry_delay_ms));
            }
        }

        ESP_LOGW(TAG, "[%s] 連續失敗，切換到下一個天氣來源", provider->name);
    }

    ESP_LOGE(TAG, "所有天氣來源皆失敗");
    return false;
}

bool weather_get_info(weather_info_t *out)
{
    if (out == NULL)
    {
        return false;
    }

    weather_init();

    if (s_weather_mutex == NULL)
    {
        memset(out, 0, sizeof(*out));
        return false;
    }

    bool valid = false;

    if (xSemaphoreTake(s_weather_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        *out = s_latest_weather;
        valid = s_latest_weather.valid;
        xSemaphoreGive(s_weather_mutex);
    }
    else
    {
        memset(out, 0, sizeof(*out));
    }

    return valid;
}