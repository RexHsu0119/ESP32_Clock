#include "sd_player_logic.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "decoder/esp_audio_dec_default.h"
#include "decoder/impl/esp_aac_dec.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "simple_dec/esp_audio_simple_dec.h"
#include "simple_dec/esp_audio_simple_dec_default.h"

#include "audio_output.h"
#include "ui.h"

#define SD_PLAYER_MOUNT_POINT "/usb"
#define SD_PLAYER_READ_BUF_SIZE 1024
#define SD_PLAYER_PCM_BUF_SIZE 4096
#define SD_PLAYER_TASK_STACK_SIZE 8192
#define SD_PLAYER_TASK_PRIORITY 4
#define SD_PLAYER_TASK_CORE_ID 1
#define SD_PLAYER_PATH_MAX_LEN 160

static bool s_sd_decoders_registered = false;

static const char *sd_player_log_tag(const sd_player_logic_context_t *ctx)
{
    return (ctx != NULL && ctx->log_tag != NULL) ? ctx->log_tag : "SD_PLAY";
}

static bool sd_player_context_valid(const sd_player_logic_context_t *ctx)
{
    return ctx != NULL &&
           ctx->sd_player_mode != NULL &&
           ctx->sd_player_playing != NULL &&
           ctx->sd_player_connecting != NULL &&
           ctx->sd_player_selected_file != NULL &&
           ctx->sd_player_active_file != NULL &&
           ctx->sd_player_file_count != NULL &&
           ctx->sd_player_stop_requested != NULL &&
           ctx->sd_player_task_handle != NULL &&
           ctx->sd_player_status_text != NULL &&
           ctx->sd_player_status_text_size > 0 &&
           ctx->sd_player_filenames != NULL &&
           ctx->sd_player_filenames_count > 0 &&
           ctx->current_panel != NULL &&
           ctx->last_clock_panel_before_overlay != NULL;
}

static void sd_player_set_status(sd_player_logic_context_t *ctx, const char *text)
{
    if (!sd_player_context_valid(ctx))
    {
        return;
    }
    if (text == NULL)
    {
        text = "Idle";
    }
    snprintf(ctx->sd_player_status_text, ctx->sd_player_status_text_size, "%s", text);
}

static bool sd_player_is_audio_ext(const char *name)
{
    size_t len = strlen(name);
    if (len < 5)
    {
        return false;
    }
    const char *ext = name + len - 4;
    return strcasecmp(ext, ".mp3") == 0 || strcasecmp(ext, ".wav") == 0;
}

static esp_audio_simple_dec_type_t sd_player_dec_type_for_file(const char *name)
{
    size_t len = strlen(name);
    if (len >= 4 && strcasecmp(name + len - 4, ".wav") == 0)
    {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_WAV;
    }
    return ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
}

static const char *sd_player_decode_fail_status(esp_audio_simple_dec_type_t type)
{
    return (type == ESP_AUDIO_SIMPLE_DEC_TYPE_WAV) ? "WAV decode fail" : "MP3 decode fail";
}

static void sd_player_scan_files(sd_player_logic_context_t *ctx)
{
    DIR *dir;
    struct dirent *entry;
    int count = 0;
    int total_entries = 0;

    if (!sd_player_context_valid(ctx))
    {
        return;
    }

    *ctx->sd_player_file_count = 0;
    ESP_LOGI("SD_PLAY", "[DEBUG] Scanning for files at %s", SD_PLAYER_MOUNT_POINT);

    dir = opendir(SD_PLAYER_MOUNT_POINT);
    if (dir == NULL)
    {
        ESP_LOGE("SD_PLAY", "[DEBUG] Failed to open %s (errno=%d)", SD_PLAYER_MOUNT_POINT, errno);
        sd_player_set_status(ctx, "No USB drive");
        return;
    }

    ESP_LOGI("SD_PLAY", "[DEBUG] Directory opened successfully");

    while ((entry = readdir(dir)) != NULL && count < ctx->sd_player_filenames_count)
    {
        size_t file_name_len;

        total_entries++;
        if (entry->d_type != DT_REG && entry->d_type != DT_UNKNOWN)
        {
            continue;
        }
        if (!sd_player_is_audio_ext(entry->d_name))
        {
            continue;
        }

        file_name_len = strnlen(entry->d_name, SD_PLAYER_FILENAME_MAX_LEN - 1U);
        memcpy(ctx->sd_player_filenames[count], entry->d_name, file_name_len);
        ctx->sd_player_filenames[count][file_name_len] = '\0';
        ESP_LOGI("SD_PLAY", "[DEBUG] Added audio file #%d: %s", count + 1, ctx->sd_player_filenames[count]);
        count++;
    }

    closedir(dir);
    *ctx->sd_player_file_count = count;
    ESP_LOGI("SD_PLAY", "[DEBUG] Scan complete: found %d audio files out of %d total entries", count, total_entries);
}

static bool sd_player_register_decoders(void)
{
    esp_audio_err_t ret;

    if (s_sd_decoders_registered)
    {
        return true;
    }

    ret = esp_audio_dec_register_default();
    if (ret != ESP_AUDIO_ERR_OK && ret != ESP_AUDIO_ERR_ALREADY_EXIST)
    {
        return false;
    }

    ret = esp_audio_simple_dec_register_default();
    if (ret != ESP_AUDIO_ERR_OK && ret != ESP_AUDIO_ERR_ALREADY_EXIST)
    {
        return false;
    }

    s_sd_decoders_registered = true;
    return true;
}

static bool sd_player_write_pcm(sd_player_logic_context_t *ctx,
                                const uint8_t *pcm_data,
                                size_t pcm_size,
                                uint32_t sample_rate_hz,
                                uint8_t channel_count,
                                uint8_t bits_per_sample)
{
    const int16_t *samples = (const int16_t *)pcm_data;
    size_t frame_count;

    if (!sd_player_context_valid(ctx) || pcm_data == NULL || pcm_size == 0 ||
        sample_rate_hz == 0 || bits_per_sample != 16 ||
        (channel_count != 1 && channel_count != 2))
    {
        return false;
    }

    if (audio_output_set_sample_rate(sample_rate_hz) != ESP_OK)
    {
        sd_player_set_status(ctx, "Audio init fail");
        return false;
    }

    if (channel_count == 2)
    {
        frame_count = pcm_size / (sizeof(int16_t) * 2U);
        return frame_count > 0 && audio_output_write_stereo_16(samples, frame_count, 1000) == ESP_OK;
    }

    frame_count = pcm_size / sizeof(int16_t);
    if (frame_count == 0)
    {
        return true;
    }

    int16_t *stereo_buf = (int16_t *)malloc(frame_count * 2U * sizeof(int16_t));
    if (stereo_buf == NULL)
    {
        sd_player_set_status(ctx, "Audio mem fail");
        return false;
    }

    for (size_t index = 0; index < frame_count; index++)
    {
        stereo_buf[index * 2] = samples[index];
        stereo_buf[index * 2 + 1] = samples[index];
    }

    bool ok = audio_output_write_stereo_16(stereo_buf, frame_count, 1000) == ESP_OK;
    free(stereo_buf);
    return ok;
}

static bool sd_player_play_file(sd_player_logic_context_t *ctx, int file_index)
{
    esp_audio_simple_dec_handle_t decoder = NULL;
    esp_audio_simple_dec_info_t dec_info = {0};
    esp_audio_simple_dec_cfg_t dec_cfg;
    esp_aac_dec_cfg_t aac_cfg;
    char path[SD_PLAYER_PATH_MAX_LEN];
    FILE *fp = NULL;
    uint8_t *raw_buf = NULL;
    uint8_t *pcm_buf = NULL;
    size_t pcm_buf_size = SD_PLAYER_PCM_BUF_SIZE;
    size_t raw_data_len = 0;
    bool input_eof = false;
    bool audio_ready = false;
    bool started = false;
    bool success = false;
    esp_audio_simple_dec_type_t dec_type;
    const char *filename;

    if (!sd_player_context_valid(ctx) || file_index < 0 || file_index >= *ctx->sd_player_file_count)
    {
        return false;
    }

    filename = ctx->sd_player_filenames[file_index];
    dec_type = sd_player_dec_type_for_file(filename);
    snprintf(path, sizeof(path), "%s/%s", SD_PLAYER_MOUNT_POINT, filename);

    if (!sd_player_register_decoders())
    {
        sd_player_set_status(ctx, "Decoder init fail");
        return false;
    }

    memset(&dec_cfg, 0, sizeof(dec_cfg));
    dec_cfg.dec_type = dec_type;
    dec_cfg.use_frame_dec = false;

    if (dec_type == ESP_AUDIO_SIMPLE_DEC_TYPE_AAC)
    {
        aac_cfg = (esp_aac_dec_cfg_t)ESP_AAC_DEC_CONFIG_DEFAULT();
        aac_cfg.aac_plus_enable = false;
        dec_cfg.dec_cfg = &aac_cfg;
        dec_cfg.cfg_size = sizeof(aac_cfg);
    }

    fp = fopen(path, "rb");
    if (fp == NULL)
    {
        sd_player_set_status(ctx, "File open fail");
        return false;
    }

    if (esp_audio_simple_dec_open(&dec_cfg, &decoder) != ESP_AUDIO_ERR_OK)
    {
        sd_player_set_status(ctx, "Decoder open fail");
        goto cleanup;
    }

    raw_buf = (uint8_t *)malloc(SD_PLAYER_READ_BUF_SIZE);
    pcm_buf = (uint8_t *)malloc(pcm_buf_size);
    if (raw_buf == NULL || pcm_buf == NULL)
    {
        sd_player_set_status(ctx, "Audio mem fail");
        goto cleanup;
    }

    while (!*ctx->sd_player_stop_requested)
    {
        esp_audio_simple_dec_raw_t raw;
        bool made_progress = false;

        if (!input_eof && raw_data_len < SD_PLAYER_READ_BUF_SIZE)
        {
            size_t read_len = fread(raw_buf + raw_data_len, 1, SD_PLAYER_READ_BUF_SIZE - raw_data_len, fp);
            raw_data_len += read_len;
            if (read_len == 0)
            {
                if (feof(fp))
                {
                    input_eof = true;
                }
                else
                {
                    sd_player_set_status(ctx, "Read error");
                    goto cleanup;
                }
            }
        }

        if (raw_data_len == 0)
        {
            sd_player_set_status(ctx, started ? "Done" : "Empty file");
            success = true;
            goto cleanup;
        }

        raw.buffer = raw_buf;
        raw.len = (uint32_t)raw_data_len;
        raw.eos = input_eof;
        raw.consumed = 0;
        raw.frame_recover = ESP_AUDIO_SIMPLE_DEC_RECOVERY_NONE;

        while (raw.len > 0 && !*ctx->sd_player_stop_requested)
        {
            esp_audio_simple_dec_out_t out;
            esp_audio_err_t dec_ret;

            out.buffer = pcm_buf;
            out.len = (uint32_t)pcm_buf_size;
            out.needed_size = 0;
            out.decoded_size = 0;

            dec_ret = esp_audio_simple_dec_process(decoder, &raw, &out);
            if (dec_ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH)
            {
                uint8_t *new_buf = (uint8_t *)realloc(pcm_buf, out.needed_size);
                if (new_buf == NULL)
                {
                    sd_player_set_status(ctx, "Audio mem fail");
                    goto cleanup;
                }
                pcm_buf = new_buf;
                pcm_buf_size = out.needed_size;
                continue;
            }
            if (dec_ret != ESP_AUDIO_ERR_OK)
            {
                sd_player_set_status(ctx, sd_player_decode_fail_status(dec_type));
                goto cleanup;
            }

            if (out.decoded_size > 0)
            {
                made_progress = true;
                if (!audio_ready)
                {
                    if (esp_audio_simple_dec_get_info(decoder, &dec_info) != ESP_AUDIO_ERR_OK ||
                        dec_info.sample_rate == 0 || dec_info.bits_per_sample != 16 ||
                        (dec_info.channel != 1 && dec_info.channel != 2))
                    {
                        sd_player_set_status(ctx, "Unsupported audio");
                        goto cleanup;
                    }
                    *ctx->sd_player_connecting = false;
                    *ctx->sd_player_playing = true;
                    sd_player_set_status(ctx, "Playing");
                    ui_refresh();
                    started = true;
                    audio_ready = true;
                }

                if (!sd_player_write_pcm(ctx,
                                         out.buffer,
                                         out.decoded_size,
                                         dec_info.sample_rate,
                                         dec_info.channel,
                                         dec_info.bits_per_sample))
                {
                    sd_player_set_status(ctx, "Audio write fail");
                    goto cleanup;
                }
            }

            if (raw.consumed > 0)
            {
                made_progress = true;
            }
            if (raw.consumed == 0 && out.decoded_size == 0)
            {
                break;
            }

            raw.buffer += raw.consumed;
            raw.len -= raw.consumed;
        }

        raw_data_len = raw.len;
        if (raw_data_len > 0 && raw.buffer != raw_buf)
        {
            memmove(raw_buf, raw.buffer, raw_data_len);
        }
        if (input_eof && raw_data_len == 0)
        {
            sd_player_set_status(ctx, started ? "Done" : "Empty file");
            success = true;
            goto cleanup;
        }
        if (!made_progress)
        {
            if (raw_data_len == SD_PLAYER_READ_BUF_SIZE)
            {
                sd_player_set_status(ctx, "Stream buffer full");
                goto cleanup;
            }
            if (input_eof)
            {
                sd_player_set_status(ctx, sd_player_decode_fail_status(dec_type));
                goto cleanup;
            }
        }
    }

    success = true;

cleanup:
    audio_output_stop();
    if (decoder != NULL)
    {
        esp_audio_simple_dec_close(decoder);
    }
    free(raw_buf);
    free(pcm_buf);
    if (fp != NULL)
    {
        fclose(fp);
    }
    if (!success && started)
    {
        *ctx->sd_player_playing = false;
    }
    return success;
}

static void sd_player_task(void *arg)
{
    sd_player_logic_context_t *ctx = (sd_player_logic_context_t *)arg;
    int file_index;

    if (!sd_player_context_valid(ctx))
    {
        vTaskDelete(NULL);
        return;
    }

    file_index = *ctx->sd_player_selected_file;
    *ctx->sd_player_active_file = file_index;
    *ctx->sd_player_connecting = true;
    *ctx->sd_player_playing = false;
    *ctx->sd_player_stop_requested = false;
    sd_player_set_status(ctx, "Loading...");
    ui_refresh();

    ESP_LOGI(sd_player_log_tag(ctx), "Playing file %d: %s",
             file_index,
             sd_player_logic_filename(ctx, file_index));

    (void)sd_player_play_file(ctx, file_index);

    if (*ctx->sd_player_stop_requested)
    {
        sd_player_set_status(ctx, "Stopped");
    }

    *ctx->sd_player_connecting = false;
    *ctx->sd_player_playing = false;
    *ctx->sd_player_active_file = -1;
    *ctx->sd_player_stop_requested = false;
    *ctx->sd_player_task_handle = NULL;
    ui_refresh();
    vTaskDelete(NULL);
}

const char *sd_player_logic_filename(const sd_player_logic_context_t *ctx, int index)
{
    if (!sd_player_context_valid(ctx) || index < 0 || index >= *ctx->sd_player_file_count)
    {
        return "";
    }
    return ctx->sd_player_filenames[index];
}

int sd_player_logic_file_count(const sd_player_logic_context_t *ctx)
{
    if (!sd_player_context_valid(ctx))
    {
        return 0;
    }
    return *ctx->sd_player_file_count;
}

void sd_player_logic_enter_mode(sd_player_logic_context_t *ctx)
{
    if (!sd_player_context_valid(ctx))
    {
        return;
    }

    *ctx->last_clock_panel_before_overlay = *ctx->current_panel;
    *ctx->sd_player_mode = true;

    if (!*ctx->sd_player_playing && !*ctx->sd_player_connecting)
    {
        sd_player_scan_files(ctx);
        if (*ctx->sd_player_file_count == 0)
        {
            sd_player_set_status(ctx, "No files");
        }
        else
        {
            sd_player_set_status(ctx, "Idle");
            if (*ctx->sd_player_selected_file >= *ctx->sd_player_file_count)
            {
                *ctx->sd_player_selected_file = 0;
            }
        }
    }

    ui_refresh();
}

void sd_player_logic_exit_mode(sd_player_logic_context_t *ctx)
{
    if (!sd_player_context_valid(ctx))
    {
        return;
    }

    sd_player_logic_force_stop(ctx, "Stopped");
    *ctx->sd_player_mode = false;
    *ctx->current_panel = *ctx->last_clock_panel_before_overlay;
    ui_refresh();
}

void sd_player_logic_move_file(sd_player_logic_context_t *ctx, int delta)
{
    int count;
    int selected;

    if (!sd_player_context_valid(ctx))
    {
        return;
    }

    count = *ctx->sd_player_file_count;
    if (count <= 0)
    {
        return;
    }

    selected = *ctx->sd_player_selected_file + delta;
    while (selected < 0)
    {
        selected += count;
    }
    while (selected >= count)
    {
        selected -= count;
    }

    *ctx->sd_player_selected_file = selected;
    if (!*ctx->sd_player_playing && !*ctx->sd_player_connecting)
    {
        sd_player_set_status(ctx, "Idle");
    }
    ui_refresh();
}

void sd_player_logic_toggle_playback(sd_player_logic_context_t *ctx)
{
    if (!sd_player_context_valid(ctx))
    {
        return;
    }

    if (*ctx->sd_player_playing || *ctx->sd_player_connecting)
    {
        sd_player_logic_force_stop(ctx, "Stopping...");
        return;
    }
    if (*ctx->sd_player_task_handle != NULL)
    {
        return;
    }
    if (*ctx->sd_player_file_count <= 0)
    {
        sd_player_set_status(ctx, "No files");
        ui_refresh();
        return;
    }

    ESP_LOGI(sd_player_log_tag(ctx),
             "Create playback task, free_heap=%u, largest_block=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    if (xTaskCreatePinnedToCore(sd_player_task,
                                "sd_player",
                                SD_PLAYER_TASK_STACK_SIZE,
                                (void *)ctx,
                                SD_PLAYER_TASK_PRIORITY,
                                ctx->sd_player_task_handle,
                                SD_PLAYER_TASK_CORE_ID) != pdPASS)
    {
        ESP_LOGE(sd_player_log_tag(ctx),
                 "Create playback task failed, free_heap=%u, largest_block=%u",
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        sd_player_set_status(ctx, "Low memory");
        *ctx->sd_player_task_handle = NULL;
        ui_refresh();
    }
}

void sd_player_logic_force_stop(sd_player_logic_context_t *ctx, const char *reason)
{
    if (!sd_player_context_valid(ctx))
    {
        return;
    }

    if (!*ctx->sd_player_playing && !*ctx->sd_player_connecting && *ctx->sd_player_task_handle == NULL)
    {
        if (reason != NULL)
        {
            sd_player_set_status(ctx, reason);
        }
        return;
    }

    *ctx->sd_player_stop_requested = true;
    *ctx->sd_player_connecting = false;
    *ctx->sd_player_playing = false;
    if (reason != NULL)
    {
        sd_player_set_status(ctx, reason);
    }
    ui_refresh();
}
