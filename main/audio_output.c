#include "audio_output.h"

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#define AUDIO_I2S_BCLK_GPIO GPIO_NUM_15
#define AUDIO_I2S_WS_GPIO GPIO_NUM_16
#define AUDIO_I2S_DOUT_GPIO GPIO_NUM_7

#define AUDIO_OUTPUT_DEFAULT_SAMPLE_RATE_HZ 16000U
#define AUDIO_I2S_DMA_FALLBACK_COUNT 4

static i2s_chan_handle_t s_audio_tx_chan = NULL;
static SemaphoreHandle_t s_audio_mutex = NULL;
static uint32_t s_audio_sample_rate_hz = 0;

static esp_err_t audio_output_ensure_mutex(void)
{
    if (s_audio_mutex != NULL)
    {
        return ESP_OK;
    }

    s_audio_mutex = xSemaphoreCreateMutex();
    return (s_audio_mutex != NULL) ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t audio_output_recreate_channel_locked(uint32_t sample_rate_hz)
{
    esp_err_t ret;
    uint32_t dma_free;
    uint32_t internal_free;
    static const uint16_t dma_desc_candidates[AUDIO_I2S_DMA_FALLBACK_COUNT] = {8, 6, 4, 3};
    static const uint16_t dma_frame_candidates[AUDIO_I2S_DMA_FALLBACK_COUNT] = {128, 128, 128, 128};

    if (sample_rate_hz == 0)
    {
        sample_rate_hz = AUDIO_OUTPUT_DEFAULT_SAMPLE_RATE_HZ;
    }

    if (s_audio_tx_chan != NULL)
    {
        i2s_channel_disable(s_audio_tx_chan);
        i2s_del_channel(s_audio_tx_chan);
        s_audio_tx_chan = NULL;
    }

    for (int i = 0; i < AUDIO_I2S_DMA_FALLBACK_COUNT; i++)
    {
        i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
        i2s_std_config_t std_cfg = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_hz),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
            .gpio_cfg = {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = AUDIO_I2S_BCLK_GPIO,
                .ws = AUDIO_I2S_WS_GPIO,
                .dout = AUDIO_I2S_DOUT_GPIO,
                .din = I2S_GPIO_UNUSED,
                .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv = false,
                },
            },
        };

        chan_cfg.auto_clear = true;
        chan_cfg.dma_desc_num = dma_desc_candidates[i];
        chan_cfg.dma_frame_num = dma_frame_candidates[i];

        dma_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_DMA);
        internal_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        ESP_LOGI("AUDIO_OUT",
                 "I2S recreate try %d/%d: rate=%u, dma_desc=%u, dma_frame=%u, dma_free=%u, internal_free=%u",
                 i + 1,
                 AUDIO_I2S_DMA_FALLBACK_COUNT,
                 (unsigned)sample_rate_hz,
                 (unsigned)chan_cfg.dma_desc_num,
                 (unsigned)chan_cfg.dma_frame_num,
                 (unsigned)dma_free,
                 (unsigned)internal_free);

        ret = i2s_new_channel(&chan_cfg, &s_audio_tx_chan, NULL);
        if (ret != ESP_OK)
        {
            ESP_LOGW("AUDIO_OUT", "i2s_new_channel failed at dma_desc=%u, dma_frame=%u, ret=%d",
                     (unsigned)chan_cfg.dma_desc_num,
                     (unsigned)chan_cfg.dma_frame_num,
                     ret);
            s_audio_tx_chan = NULL;
            continue;
        }

        ret = i2s_channel_init_std_mode(s_audio_tx_chan, &std_cfg);
        if (ret != ESP_OK)
        {
            ESP_LOGW("AUDIO_OUT", "i2s_channel_init_std_mode failed at dma_desc=%u, dma_frame=%u, ret=%d",
                     (unsigned)chan_cfg.dma_desc_num,
                     (unsigned)chan_cfg.dma_frame_num,
                     ret);
            i2s_del_channel(s_audio_tx_chan);
            s_audio_tx_chan = NULL;
            continue;
        }

        ret = i2s_channel_enable(s_audio_tx_chan);
        if (ret != ESP_OK)
        {
            ESP_LOGW("AUDIO_OUT", "i2s_channel_enable failed at dma_desc=%u, dma_frame=%u, ret=%d",
                     (unsigned)chan_cfg.dma_desc_num,
                     (unsigned)chan_cfg.dma_frame_num,
                     ret);
            i2s_del_channel(s_audio_tx_chan);
            s_audio_tx_chan = NULL;
            continue;
        }

        ESP_LOGI("AUDIO_OUT", "I2S recreate success with dma_desc=%u, dma_frame=%u",
                 (unsigned)chan_cfg.dma_desc_num,
                 (unsigned)chan_cfg.dma_frame_num);
        s_audio_sample_rate_hz = sample_rate_hz;
        return ESP_OK;
    }
    return ESP_ERR_NO_MEM;
}

esp_err_t audio_output_init(void)
{
    esp_err_t ret = audio_output_ensure_mutex();
    if (ret != ESP_OK)
    {
        return ret;
    }

    if (xSemaphoreTake(s_audio_mutex, portMAX_DELAY) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }

    if (s_audio_tx_chan == NULL)
    {
        ret = audio_output_recreate_channel_locked(AUDIO_OUTPUT_DEFAULT_SAMPLE_RATE_HZ);
    }

    xSemaphoreGive(s_audio_mutex);
    return ret;
}

esp_err_t audio_output_set_sample_rate(uint32_t sample_rate_hz)
{
    esp_err_t ret = audio_output_ensure_mutex();
    if (ret != ESP_OK)
    {
        return ret;
    }

    if (xSemaphoreTake(s_audio_mutex, portMAX_DELAY) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }

    if (s_audio_tx_chan == NULL || s_audio_sample_rate_hz != sample_rate_hz)
    {
        ret = audio_output_recreate_channel_locked(sample_rate_hz);
    }

    xSemaphoreGive(s_audio_mutex);
    return ret;
}

esp_err_t audio_output_write_stereo_16(const int16_t *samples,
                                       size_t frame_count,
                                       uint32_t timeout_ms)
{
    esp_err_t ret;
    size_t bytes_written = 0;
    size_t bytes_to_write;

    if (samples == NULL || frame_count == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    ret = audio_output_init();
    if (ret != ESP_OK)
    {
        return ret;
    }

    if (xSemaphoreTake(s_audio_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }

    bytes_to_write = frame_count * sizeof(int16_t) * 2U;
    ret = i2s_channel_write(s_audio_tx_chan,
                            samples,
                            bytes_to_write,
                            &bytes_written,
                            pdMS_TO_TICKS(timeout_ms));

    xSemaphoreGive(s_audio_mutex);

    if (ret != ESP_OK)
    {
        return ret;
    }

    return (bytes_written == bytes_to_write) ? ESP_OK : ESP_FAIL;
}

esp_err_t audio_output_stop(void)
{
    static const int16_t silence[64] = {0};
    return audio_output_write_stereo_16(silence, 32, 50);
}

esp_err_t audio_output_deinit(void)
{
    esp_err_t ret = audio_output_ensure_mutex();
    if (ret != ESP_OK)
    {
        return ret;
    }

    if (xSemaphoreTake(s_audio_mutex, portMAX_DELAY) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }

    if (s_audio_tx_chan != NULL)
    {
        i2s_channel_disable(s_audio_tx_chan);
        i2s_del_channel(s_audio_tx_chan);
        s_audio_tx_chan = NULL;
    }
    s_audio_sample_rate_hz = 0;

    xSemaphoreGive(s_audio_mutex);
    return ESP_OK;
}