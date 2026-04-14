#include "alarm_runtime.h"

#include <math.h>
#include <string.h>
#include <time.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "alarm_logic.h"
#include "clock_types.h"
#include "ui.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define AUDIO_I2S_BCLK_GPIO GPIO_NUM_15
#define AUDIO_I2S_WS_GPIO GPIO_NUM_16
#define AUDIO_I2S_DOUT_GPIO GPIO_NUM_7

#define AUDIO_SAMPLE_RATE_HZ 16000
#define AUDIO_TONE_FREQ_HZ 1000
#define AUDIO_AMPLITUDE 12000
#define AUDIO_FRAME_SAMPLES 256

static i2s_chan_handle_t s_audio_tx_chan = NULL;
static bool s_audio_inited = false;

static const char *alarm_runtime_log_tag(const alarm_runtime_context_t *ctx)
{
    return (ctx != NULL && ctx->log_tag != NULL) ? ctx->log_tag : "ALARM_RUNTIME";
}

static bool alarm_runtime_context_valid(const alarm_runtime_context_t *ctx)
{
    return ctx != NULL && ctx->app != NULL;
}

static void audio_i2s_init(void)
{
    if (s_audio_inited)
    {
        return;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_audio_tx_chan, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE_HZ),
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

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_audio_tx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_audio_tx_chan));

    s_audio_inited = true;
}

static void audio_play_tone_ms(int freq_hz, int duration_ms)
{
    if (!s_audio_inited || s_audio_tx_chan == NULL)
    {
        return;
    }

    int16_t buffer[AUDIO_FRAME_SAMPLES * 2];
    size_t bytes_written = 0;

    const int total_samples = (AUDIO_SAMPLE_RATE_HZ * duration_ms) / 1000;
    const float phase_step = 2.0f * (float)M_PI * (float)freq_hz / (float)AUDIO_SAMPLE_RATE_HZ;

    float phase = 0.0f;
    int generated = 0;

    while (generated < total_samples)
    {
        int samples_this_round = AUDIO_FRAME_SAMPLES;
        if (samples_this_round > (total_samples - generated))
        {
            samples_this_round = total_samples - generated;
        }

        for (int i = 0; i < samples_this_round; i++)
        {
            int16_t sample = (int16_t)(sinf(phase) * AUDIO_AMPLITUDE);

            buffer[i * 2 + 0] = sample;
            buffer[i * 2 + 1] = sample;

            phase += phase_step;
            if (phase >= 2.0f * (float)M_PI)
            {
                phase -= 2.0f * (float)M_PI;
            }
        }

        ESP_ERROR_CHECK(i2s_channel_write(
            s_audio_tx_chan,
            buffer,
            samples_this_round * sizeof(int16_t) * 2,
            &bytes_written,
            portMAX_DELAY));

        generated += samples_this_round;
    }
}

static void audio_play_silence_ms(int duration_ms)
{
    if (!s_audio_inited || s_audio_tx_chan == NULL)
    {
        return;
    }

    int16_t buffer[AUDIO_FRAME_SAMPLES * 2];
    memset(buffer, 0, sizeof(buffer));

    size_t bytes_written = 0;
    const int total_samples = (AUDIO_SAMPLE_RATE_HZ * duration_ms) / 1000;
    int generated = 0;

    while (generated < total_samples)
    {
        int samples_this_round = AUDIO_FRAME_SAMPLES;
        if (samples_this_round > (total_samples - generated))
        {
            samples_this_round = total_samples - generated;
        }

        ESP_ERROR_CHECK(i2s_channel_write(
            s_audio_tx_chan,
            buffer,
            samples_this_round * sizeof(int16_t) * 2,
            &bytes_written,
            portMAX_DELAY));

        generated += samples_this_round;
    }
}

static void audio_play_beep_ms(int duration_ms)
{
    audio_i2s_init();
    audio_play_tone_ms(AUDIO_TONE_FREQ_HZ, duration_ms);
    audio_play_silence_ms(20);
}

static void alarm_mark_triggered(app_context_t *app, const struct tm *t)
{
    app->alarm_last_trigger_year = t->tm_year;
    app->alarm_last_trigger_yday = t->tm_yday;
    app->alarm_last_trigger_hour = t->tm_hour;
    app->alarm_last_trigger_minute = t->tm_min;
}

static bool alarm_same_as_last_trigger(const app_context_t *app, const struct tm *t)
{
    return app->alarm_last_trigger_year == t->tm_year &&
           app->alarm_last_trigger_yday == t->tm_yday &&
           app->alarm_last_trigger_hour == t->tm_hour &&
           app->alarm_last_trigger_minute == t->tm_min;
}

static void alarm_set_background_locked(lv_color_t color)
{
    lv_obj_t *scr = lv_screen_active();
    if (scr != NULL)
    {
        lv_obj_set_style_bg_color(scr, color, 0);
    }
}

void alarm_runtime_apply_background_state_locked(const alarm_runtime_context_t *ctx)
{
    app_context_t *app;

    if (!alarm_runtime_context_valid(ctx))
    {
        return;
    }

    app = ctx->app;
    if (app->alarm_ringing)
    {
        alarm_set_background_locked(app->alarm_flash_on ? lv_color_hex(0x707000) : lv_color_hex(0x000000));
    }
    else
    {
        alarm_set_background_locked(lv_color_hex(0x000000));
    }
}

static void alarm_sound_task(void *arg)
{
    alarm_runtime_context_t *ctx = (alarm_runtime_context_t *)arg;
    app_context_t *app = (ctx != NULL) ? ctx->app : NULL;

    if (app == NULL)
    {
        vTaskDelete(NULL);
        return;
    }

    while (app->alarm_ringing)
    {
        audio_play_beep_ms(150);
        if (!app->alarm_ringing)
        {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(220));

        audio_play_beep_ms(150);
        if (!app->alarm_ringing)
        {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(700));
    }

    app->alarm_sound_task_handle = NULL;
    vTaskDelete(NULL);
}

static void alarm_start(const alarm_runtime_context_t *ctx)
{
    app_context_t *app;

    if (!alarm_runtime_context_valid(ctx))
    {
        return;
    }

    app = ctx->app;
    if (app->alarm_ringing)
    {
        return;
    }

    app->alarm_ringing = true;
    app->alarm_flash_on = true;
    app->alarm_last_flash_us = esp_timer_get_time();

    if (app->alarm.repeat == ALARM_REPEAT_ONCE && app->alarm.enabled)
    {
        app->alarm.enabled = false;
        alarm_save_to_nvs(&app->alarm);
    }

    if (ui_lock(pdMS_TO_TICKS(100)))
    {
        alarm_runtime_apply_background_state_locked(ctx);
        ui_unlock();
    }

    if (app->alarm_sound_task_handle == NULL)
    {
        xTaskCreatePinnedToCore(alarm_sound_task, "alarm_sound", 4096, (void *)ctx, 4, &app->alarm_sound_task_handle, 1);
    }

    ESP_LOGI(alarm_runtime_log_tag(ctx), "鬧鐘開始響鈴");
}

void alarm_runtime_stop(const alarm_runtime_context_t *ctx)
{
    app_context_t *app;

    if (!alarm_runtime_context_valid(ctx))
    {
        return;
    }

    app = ctx->app;
    if (!app->alarm_ringing)
    {
        return;
    }

    app->alarm_ringing = false;
    app->alarm_flash_on = false;

    if (ui_lock(pdMS_TO_TICKS(100)))
    {
        alarm_runtime_apply_background_state_locked(ctx);
        ui_unlock();
    }

    ui_refresh();
    ESP_LOGI(alarm_runtime_log_tag(ctx), "鬧鐘已停止");
}

void alarm_runtime_check_trigger(const alarm_runtime_context_t *ctx)
{
    time_t now;
    struct tm t;
    app_context_t *app;

    if (!alarm_runtime_context_valid(ctx))
    {
        return;
    }

    app = ctx->app;
    if (app->app_mode != APP_MODE_CLOCK)
    {
        return;
    }
    if (app->is_setting_time || app->alarm_setting_mode || app->alarm_ringing)
    {
        return;
    }
    if (!app->alarm.enabled)
    {
        return;
    }
    if (!app->time_base_valid)
    {
        return;
    }

    now = time(NULL);
    if (localtime_r(&now, &t) == NULL)
    {
        return;
    }

    if (t.tm_hour == app->alarm.hour && t.tm_min == app->alarm.minute)
    {
        if (!alarm_same_as_last_trigger(app, &t))
        {
            alarm_mark_triggered(app, &t);
            alarm_start(ctx);
        }
    }
}

void alarm_runtime_update_flash_effect(const alarm_runtime_context_t *ctx)
{
    int64_t now_us;
    app_context_t *app;

    if (!alarm_runtime_context_valid(ctx))
    {
        return;
    }

    app = ctx->app;
    if (!app->alarm_ringing)
    {
        return;
    }

    now_us = esp_timer_get_time();
    if ((now_us - app->alarm_last_flash_us) >= ctx->flash_period_us)
    {
        app->alarm_last_flash_us = now_us;
        app->alarm_flash_on = !app->alarm_flash_on;

        if (ui_lock(pdMS_TO_TICKS(20)))
        {
            alarm_runtime_apply_background_state_locked(ctx);
            ui_unlock();
        }
    }
}