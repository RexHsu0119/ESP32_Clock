#include "button.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "BUTTON";

// 按鈕狀態結構體
typedef struct
{
    uint8_t gpio_num;
    uint8_t button_id;
    uint32_t press_time;
    uint8_t last_state;
} button_state_t;

// 按鈕事件結構體
typedef struct
{
    uint8_t button_id;
    uint8_t event_type;
} button_event_t;

// 按鈕配置
static button_state_t button_states[] = {
    {BUTTON_UP, BUTTON_UP, 0, 1},
    {BUTTON_DOWN, BUTTON_DOWN, 0, 1},
    {BUTTON_CENTER, BUTTON_CENTER, 0, 1}};

#define BUTTON_COUNT (sizeof(button_states) / sizeof(button_states[0]))

// 事件隊列和回調函數
static QueueHandle_t button_event_queue = NULL;
static button_callback_t button_callback = NULL;
static TaskHandle_t button_task_handle = NULL;

// 按鈕去抖動參數
#define DEBOUNCE_TIME_MS 20
#define LONG_PRESS_TIME_MS 1000
#define BUTTON_CHECK_INTERVAL 10

/**
 * @brief 按鈕掃描任務
 */
static void button_scan_task(void *arg)
{
    ESP_LOGI(TAG, "按鈕掃描任務已啟動");

    while (1)
    {
        for (int i = 0; i < BUTTON_COUNT; i++)
        {
            button_state_t *state = &button_states[i];
            uint8_t current_state = gpio_get_level(state->gpio_num);

            // 檢測按下
            if (current_state == 0 && state->last_state == 1)
            {
                state->press_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
                ESP_LOGD(TAG, "按鈕 %d 按下", state->button_id);
            }
            // 檢測釋放
            else if (current_state == 1 && state->last_state == 0)
            {
                uint32_t press_duration = (xTaskGetTickCount() * portTICK_PERIOD_MS) - state->press_time;

                if (press_duration >= LONG_PRESS_TIME_MS)
                {
                    // 長按
                    button_event_t event = {
                        .button_id = state->button_id,
                        .event_type = BUTTON_LONG_PRESS};
                    xQueueSend(button_event_queue, &event, 0);
                    ESP_LOGD(TAG, "按鈕 %d 長按 (%lu ms)", state->button_id, press_duration);
                }
                else if (press_duration >= DEBOUNCE_TIME_MS)
                {
                    // 短按
                    button_event_t event = {
                        .button_id = state->button_id,
                        .event_type = BUTTON_SHORT_PRESS};
                    xQueueSend(button_event_queue, &event, 0);
                    ESP_LOGD(TAG, "按鈕 %d 短按 (%lu ms)", state->button_id, press_duration);
                }
            }

            state->last_state = current_state;
        }

        vTaskDelay(pdMS_TO_TICKS(BUTTON_CHECK_INTERVAL));
    }
}

/**
 * @brief 按鈕事件處理任務
 */
static void button_event_task(void *arg)
{
    ESP_LOGI(TAG, "按鈕事件處理任務已啟動");

    button_event_t event;

    while (1)
    {
        if (xQueueReceive(button_event_queue, &event, portMAX_DELAY))
        {
            if (button_callback != NULL)
            {
                button_callback(event.button_id, event.event_type);
            }
        }
    }
}

void button_init(void)
{
    ESP_LOGI(TAG, "初始化按鈕模組...");

    // 配置 GPIO 引腳
    gpio_config_t io_conf = {0};
    io_conf.pin_bit_mask = (1ULL << BUTTON_UP) | (1ULL << BUTTON_DOWN) | (1ULL << BUTTON_CENTER);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "GPIO 配置失敗: %s", esp_err_to_name(ret));
        return;
    }

    // 創建事件隊列
    button_event_queue = xQueueCreate(10, sizeof(button_event_t));
    if (button_event_queue == NULL)
    {
        ESP_LOGE(TAG, "創建事件隊列失敗");
        return;
    }

    // 創建按鈕掃描任務
    ret = xTaskCreate(button_scan_task, "button_scan", 2048, NULL, 5, &button_task_handle);
    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "創建按鈕掃描任務失敗");
        return;
    }

    // 創建按鈕事件處理任務
    ret = xTaskCreate(button_event_task, "button_event", 2048, NULL, 6, NULL);
    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "創建按鈕事件處理任務失敗");
        return;
    }

    ESP_LOGI(TAG, "按鈕模組初始化成功");
    ESP_LOGI(TAG, "  UP 按鈕: GPIO %d", BUTTON_UP);
    ESP_LOGI(TAG, "  DOWN 按鈕: GPIO %d", BUTTON_DOWN);
    ESP_LOGI(TAG, "  CENTER 按鈕: GPIO %d", BUTTON_CENTER);
}

void button_register_callback(button_callback_t callback)
{
    button_callback = callback;
    ESP_LOGI(TAG, "按鈕回調函數已註冊");
}

void button_deinit(void)
{
    if (button_task_handle != NULL)
    {
        vTaskDelete(button_task_handle);
        button_task_handle = NULL;
    }

    if (button_event_queue != NULL)
    {
        vQueueDelete(button_event_queue);
        button_event_queue = NULL;
    }

    button_callback = NULL;
    ESP_LOGI(TAG, "按鈕模組已反初始化");
}