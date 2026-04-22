#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    esp_err_t audio_output_init(void);
    esp_err_t audio_output_set_sample_rate(uint32_t sample_rate_hz);
    esp_err_t audio_output_write_stereo_16(const int16_t *samples,
                                           size_t frame_count,
                                           uint32_t timeout_ms);
    esp_err_t audio_output_stop(void);
    esp_err_t audio_output_deinit(void);

#ifdef __cplusplus
}
#endif