#include "weather.h"

#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

#include "wifi.h"

static const char *TAG = "WEATHER";

/* 新竹縣湖口鄉附近經緯度 */
#define WEATHER_URL \
    "https://api.open-meteo.com/v1/forecast?latitude=24.903&longitude=121.044&current=temperature_2m,relative_humidity_2m&timezone=Asia%2FTaipei"

#define WEATHER_HTTP_TIMEOUT_MS 10000
#define WEATHER_HTTP_RX_BUF_SIZE 4096
#define WEATHER_HTTP_TX_BUF_SIZE 1024
#define WEATHER_RETRY_COUNT 3
#define WEATHER_RETRY_DELAY_MS 1000

typedef struct
{
    char *buffer;
    size_t capacity;
    size_t length;
    bool overflow;
} http_resp_ctx_t;

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

static bool weather_update_once(void)
{
    char response_buffer[2048] = {0};
    http_resp_ctx_t ctx = {
        .buffer = response_buffer,
        .capacity = sizeof(response_buffer),
        .length = 0,
        .overflow = false,
    };

    esp_http_client_config_t config = {
        .url = WEATHER_URL,
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
        ESP_LOGE(TAG, "建立 HTTP client 失敗");
        return false;
    }

    esp_err_t ret = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP 請求失敗: %s", esp_err_to_name(ret));
        return false;
    }

    if (status_code != 200)
    {
        ESP_LOGE(TAG, "天氣 API HTTP 狀態碼錯誤: %d", status_code);
        return false;
    }

    if (ctx.overflow)
    {
        ESP_LOGE(TAG, "天氣回應資料過大，buffer 不足");
        return false;
    }

    cJSON *root = cJSON_Parse(response_buffer);
    if (root == NULL)
    {
        ESP_LOGE(TAG, "解析天氣 JSON 失敗");
        return false;
    }

    cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
    cJSON *temperature = cJSON_GetObjectItemCaseSensitive(current, "temperature_2m");
    cJSON *humidity = cJSON_GetObjectItemCaseSensitive(current, "relative_humidity_2m");

    if (!cJSON_IsObject(current) || !cJSON_IsNumber(temperature) || !cJSON_IsNumber(humidity))
    {
        ESP_LOGE(TAG, "天氣 JSON 欄位不完整");
        cJSON_Delete(root);
        return false;
    }

    float temp_c = (float)temperature->valuedouble;
    int hum_percent = (int)(humidity->valuedouble + 0.5f);

    if (xSemaphoreTake(s_weather_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        s_latest_weather.valid = true;
        s_latest_weather.temperature_c = temp_c;
        s_latest_weather.humidity_percent = hum_percent;
        xSemaphoreGive(s_weather_mutex);
    }

    cJSON_Delete(root);

    ESP_LOGI(TAG, "湖口天氣更新成功: %.1f C, %d%%RH", temp_c, hum_percent);
    return true;
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

    for (int attempt = 1; attempt <= WEATHER_RETRY_COUNT; attempt++)
    {
        if (weather_update_once())
        {
            return true;
        }

        if (attempt < WEATHER_RETRY_COUNT)
        {
            ESP_LOGW(TAG, "天氣更新失敗，準備重試... (%d/%d)",
                     attempt, WEATHER_RETRY_COUNT);
            vTaskDelay(pdMS_TO_TICKS(WEATHER_RETRY_DELAY_MS));
        }
    }

    ESP_LOGE(TAG, "天氣更新最終失敗");
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