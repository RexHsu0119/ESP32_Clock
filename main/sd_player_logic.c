#include "sd_player_logic.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "decoder/esp_audio_dec_default.h"
#include "decoder/impl/esp_mp3_dec.h"
#include "decoder/impl/esp_aac_dec.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "simple_dec/esp_audio_simple_dec.h"
#include "simple_dec/esp_audio_simple_dec_default.h"
#include "simple_dec/esp_audio_simple_dec_reg.h"
#include "simple_dec/esp_es_parse_types.h"

#include "audio_output.h"
#include "ui.h"

#define SD_PLAYER_MOUNT_POINT "/usb"
#define SD_PLAYER_READ_BUF_SIZE 1024
#define SD_PLAYER_PCM_BUF_SIZE 2048
#define SD_PLAYER_TASK_STACK_SIZE 6144
#define SD_PLAYER_TASK_PRIORITY 4
#define SD_PLAYER_TASK_CORE_ID 1
#define SD_PLAYER_PATH_MAX_LEN 160
#define SD_PLAYER_SYNC_SCAN_TOTAL_BYTES 8192
#define SD_PLAYER_SYNC_SCAN_CHUNK_SIZE 512
#define SD_PLAYER_MP3_HEADER_LOG_LIMIT 3

static bool s_sd_decoders_registered = false;
static bool s_sd_mp3_parser_registered = false;
static int s_sd_mp3_header_log_count = 0;

static bool sd_player_is_valid_mp3_header(const uint8_t *h);
static bool sd_player_mp3_profile_supported(FILE *fp);
static bool sd_player_is_xing_info_frame(FILE *fp, long sync_pos, const uint8_t *header);
static bool sd_player_validate_mp3_frame_chain(FILE *fp, long first_sync_pos, int frame_count);

static bool sd_player_get_mp3_version(uint8_t version_bits, uint8_t *version_idx)
{
    if (version_idx == NULL)
    {
        return false;
    }

    if (version_bits == 0x03)
    {
        *version_idx = 0;
        return true;
    }
    if (version_bits == 0x02)
    {
        *version_idx = 1;
        return true;
    }
    if (version_bits == 0x00)
    {
        *version_idx = 2;
        return true;
    }
    return false;
}

static bool sd_player_parse_mp3_layer3_header(const uint8_t *header,
                                              uint32_t *frame_size,
                                              int *sample_rate,
                                              int *bitrate,
                                              int *samples_per_frame,
                                              uint8_t *channel_count,
                                              uint8_t *channel_mode_out)
{
    static const uint16_t bitrate_table[3][16] = {
        {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0},
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
    };
    static const uint32_t sample_rate_table[3][4] = {
        {44100, 48000, 32000, 0},
        {22050, 24000, 16000, 0},
        {11025, 12000, 8000, 0},
    };
    uint8_t version_bits;
    uint8_t version_idx;
    uint8_t layer_bits;
    uint8_t bitrate_index;
    uint8_t sample_rate_index;
    uint8_t padding_bit;
    uint8_t channel_mode;
    int local_sample_rate;
    int local_bitrate;
    int local_samples_per_frame;
    uint32_t local_frame_size;

    if (header == NULL)
    {
        return false;
    }
    if (header[0] != 0xFF || (header[1] & 0xE0) != 0xE0)
    {
        return false;
    }

    version_bits = (header[1] >> 3) & 0x03;
    layer_bits = (header[1] >> 1) & 0x03;
    bitrate_index = (header[2] >> 4) & 0x0F;
    sample_rate_index = (header[2] >> 2) & 0x03;
    padding_bit = (header[2] >> 1) & 0x01;
    channel_mode = (header[3] >> 6) & 0x03;

    if (!sd_player_get_mp3_version(version_bits, &version_idx))
    {
        return false;
    }

    /* esp_mp3_dec expects actual MP3 Layer III frames. */
    if (layer_bits != 0x01)
    {
        return false;
    }

    if (bitrate_index == 0x00 || bitrate_index == 0x0F || sample_rate_index == 0x03)
    {
        return false;
    }

    local_sample_rate = (int)sample_rate_table[version_idx][sample_rate_index];
    if (local_sample_rate == 0)
    {
        return false;
    }

    local_bitrate = (int)bitrate_table[version_idx][bitrate_index];
    if (local_bitrate == 0)
    {
        return false;
    }

    local_samples_per_frame = (version_idx == 0U) ? 1152 : 576;
    local_frame_size = ((uint32_t)local_samples_per_frame / 8U * (uint32_t)local_bitrate * 1000U) / (uint32_t)local_sample_rate + padding_bit;
    if (local_frame_size < 24U)
    {
        return false;
    }

    if (s_sd_mp3_header_log_count < SD_PLAYER_MP3_HEADER_LOG_LIMIT)
    {
        ESP_LOGI("SD_PLAY", "[DEBUG] MP3 hdr: ver=%d, bitrate=%dkbps, sr=%dHz, ch_mode=%d, frame_sz=%u",
                 version_idx, local_bitrate, local_sample_rate, channel_mode, local_frame_size);
        s_sd_mp3_header_log_count++;
    }

    if (frame_size != NULL)
    {
        *frame_size = local_frame_size;
    }
    if (sample_rate != NULL)
    {
        *sample_rate = local_sample_rate;
    }
    if (bitrate != NULL)
    {
        *bitrate = local_bitrate * 1000;
    }
    if (samples_per_frame != NULL)
    {
        *samples_per_frame = local_samples_per_frame;
    }
    if (channel_count != NULL)
    {
        *channel_count = ((header[3] >> 6) == 0x03) ? 1U : 2U;
    }
    if (channel_mode_out != NULL)
    {
        *channel_mode_out = channel_mode;
    }

    return true;
}

static esp_es_parse_err_t sd_player_mp3_parse_frame(esp_es_parse_raw_t *in,
                                                     esp_es_parse_frame_info_t *frame_info)
{
    uint8_t *header;
    uint8_t channel_count;
    uint32_t frame_size;
    int sample_rate;
    int bitrate;
    int samples_per_frame;

    if (in == NULL || frame_info == NULL)
    {
        return ESP_ES_PARSE_ERR_INVALID_ARG;
    }
    if (in->len < 4)
    {
        return ESP_ES_PARSE_ERR_DATA_NOT_ENOUGH;
    }

    header = in->buffer;
    if (!sd_player_parse_mp3_layer3_header(header,
                                           &frame_size,
                                           &sample_rate,
                                           &bitrate,
                                           &samples_per_frame,
                                           &channel_count,
                                           NULL))
    {
        return ESP_ES_PARSE_ERR_WRONG_HEADER;
    }

    frame_info->frame_size = frame_size;
    frame_info->aud_info.sample_rate = sample_rate;
    frame_info->aud_info.sample_num = samples_per_frame;
    frame_info->aud_info.bits_per_sample = 16;
    frame_info->aud_info.channel = channel_count;
    frame_info->aud_info.bitrate = bitrate;
    return ESP_ES_PARSE_ERR_OK;
}

static bool sd_player_register_mp3_parser(void)
{
    esp_audio_simple_dec_reg_info_t reg_info;
    esp_audio_err_t ret;

    if (s_sd_mp3_parser_registered)
    {
        return true;
    }

    memset(&reg_info, 0, sizeof(reg_info));
    reg_info.decoder_ops = (esp_audio_dec_ops_t)ESP_MP3_DEC_DEFAULT_OPS();
    reg_info.parser = sd_player_mp3_parse_frame;
    reg_info.free = NULL;

    ret = esp_audio_simple_dec_register(ESP_AUDIO_SIMPLE_DEC_TYPE_MP3, &reg_info);
    if (ret != ESP_AUDIO_ERR_OK && ret != ESP_AUDIO_ERR_ALREADY_EXIST)
    {
        ESP_LOGW("SD_PLAY", "[DEBUG] MP3 parser register failed: %d", ret);
        return false;
    }

    s_sd_mp3_parser_registered = true;
    return true;
}

static bool sd_player_is_valid_mp3_header(const uint8_t *h)
{
    return sd_player_parse_mp3_layer3_header(h, NULL, NULL, NULL, NULL, NULL, NULL);
}

static bool sd_player_mp3_profile_supported(FILE *fp)
{
    uint8_t header[4];
    uint8_t channel_mode = 0;
    long current_pos;
    long sync_pos;
    const int check_frames = 16;

    if (fp == NULL)
    {
        return false;
    }

    current_pos = ftell(fp);
    if (current_pos < 0)
    {
        return false;
    }

    sync_pos = current_pos;
    for (int i = 0; i < check_frames; i++)
    {
        uint32_t frame_size = 0;

        if (fseek(fp, sync_pos, SEEK_SET) != 0)
        {
            break;
        }
        if (fread(header, 1, sizeof(header), fp) != sizeof(header))
        {
            break;
        }
        if (!sd_player_parse_mp3_layer3_header(header, &frame_size, NULL, NULL, NULL, NULL, &channel_mode))
        {
            break;
        }

        /* Current esp_mp3_dec profile on this target cannot initialize Joint Stereo streams reliably. */
        if (channel_mode == 1U)
        {
            ESP_LOGW("SD_PLAY", "[DEBUG] Unsupported MP3 profile: Joint Stereo (ch_mode=1) at frame %d", i);
            fseek(fp, current_pos, SEEK_SET);
            return false;
        }

        sync_pos += (long)frame_size;
    }

    fseek(fp, current_pos, SEEK_SET);

    return true;
}

static bool sd_player_is_xing_info_frame(FILE *fp, long sync_pos, const uint8_t *header)
{
    /*
     * XING/INFO frames are VBR metadata frames that have valid MP3 sync but contain
     * "Xing" or "Info" instead of audio. Decoder init fails (ret 10) on these.
     * They're located at fixed offsets based on MPEG version and channel mode.
     */
    uint8_t magic[4];
    int offset;
    uint8_t version_bits;
    uint8_t channel_mode;
    long current_pos;
    size_t read_len;

    if (fp == NULL || header == NULL)
    {
        return false;
    }

    /* Extract version and channel info from header */
    version_bits = (header[1] >> 3) & 0x03;
    channel_mode = (header[3] >> 6) & 0x03;  /* 0x03 = mono, else stereo */
    bool is_mono = (channel_mode == 0x03);

    /* Offset to XING/INFO magic depends on MPEG version and channel */
    if (version_bits == 0x03)
    {
        /* MPEG1 */
        offset = is_mono ? 21 : 36;
    }
    else
    {
        /* MPEG2 or MPEG2.5 */
        offset = is_mono ? 13 : 21;
    }

    current_pos = ftell(fp);
    if (fseek(fp, sync_pos + 4 + offset, SEEK_SET) != 0)
    {
        return false;
    }

    read_len = fread(magic, 1, sizeof(magic), fp);
    fseek(fp, current_pos, SEEK_SET);

    if (read_len != sizeof(magic))
    {
        return false;
    }

    /* Check for "Xing" or "Info" magic */
    bool is_xing_info = false;
    if ((magic[0] == 'X' && magic[1] == 'i' && magic[2] == 'n' && magic[3] == 'g') ||
        (magic[0] == 'I' && magic[1] == 'n' && magic[2] == 'f' && magic[3] == 'o'))
    {
        is_xing_info = true;
    }

    ESP_LOGI("SD_PLAY", "[DEBUG] XING check at offset %ld: is_mono=%d, offset=%d, magic=%c%c%c%c, is_xing=%d",
             sync_pos, is_mono, offset, magic[0], magic[1], magic[2], magic[3], is_xing_info);

    return is_xing_info;
}

static bool sd_player_validate_mp3_frame_chain(FILE *fp, long first_sync_pos, int frame_count)
{
    long restore_pos;
    long sync_pos;
    uint8_t header[4];

    if (fp == NULL || first_sync_pos < 0 || frame_count <= 0)
    {
        return false;
    }

    restore_pos = ftell(fp);
    sync_pos = first_sync_pos;

    for (int frame_index = 0; frame_index < frame_count; frame_index++)
    {
        uint32_t frame_size = 0;

        if (fseek(fp, sync_pos, SEEK_SET) != 0)
        {
            if (restore_pos >= 0)
            {
                fseek(fp, restore_pos, SEEK_SET);
            }
            return false;
        }

        if (fread(header, 1, sizeof(header), fp) != sizeof(header))
        {
            if (restore_pos >= 0)
            {
                fseek(fp, restore_pos, SEEK_SET);
            }
            return false;
        }

        if (!sd_player_parse_mp3_layer3_header(header, &frame_size, NULL, NULL, NULL, NULL, NULL))
        {
            if (restore_pos >= 0)
            {
                fseek(fp, restore_pos, SEEK_SET);
            }
            return false;
        }

        /* Reject XING/INFO frames (VBR metadata, not audio data) */
        if (frame_index == 0 && sd_player_is_xing_info_frame(fp, sync_pos, header))
        {
            if (restore_pos >= 0)
            {
                fseek(fp, restore_pos, SEEK_SET);
            }
            return false;  /* Not a valid audio frame */
        }

        sync_pos += (long)frame_size;
    }

    if (restore_pos >= 0)
    {
        fseek(fp, restore_pos, SEEK_SET);
    }
    return true;
}

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

    return strcasecmp(name + len - 4, ".mp3") == 0 ||
           strcasecmp(name + len - 4, ".wav") == 0;
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

static void sd_player_skip_mp3_id3v2(FILE *fp)
{
    uint8_t hdr[10];
    uint8_t flags;
    size_t n;

    if (fp == NULL)
    {
        return;
    }

    n = fread(hdr, 1, sizeof(hdr), fp);
    if (n < sizeof(hdr))
    {
        fseek(fp, 0, SEEK_SET);
        return;
    }

    if (hdr[0] == 'I' && hdr[1] == 'D' && hdr[2] == '3')
    {
        flags = hdr[5];
        uint32_t tag_size =
            ((uint32_t)(hdr[6] & 0x7F) << 21) |
            ((uint32_t)(hdr[7] & 0x7F) << 14) |
            ((uint32_t)(hdr[8] & 0x7F) << 7) |
            (uint32_t)(hdr[9] & 0x7F);
        uint32_t skip_size = 10U + tag_size;

        /* ID3v2 footer flag adds an extra 10-byte footer after tag frames. */
        if ((flags & 0x10U) != 0U)
        {
            skip_size += 10U;
        }

        ESP_LOGI("SD_PLAY", "[DEBUG] Skip MP3 ID3v2 tag: %u bytes", (unsigned)skip_size);
        fseek(fp, (long)skip_size, SEEK_SET);
        return;
    }

    fseek(fp, 0, SEEK_SET);
}

static bool sd_player_seek_mp3_frame_sync(FILE *fp)
{
    long start_pos;
    uint8_t scan_buf[SD_PLAYER_SYNC_SCAN_CHUNK_SIZE + 3U];
    size_t carry_len = 0;
    size_t total_scanned = 0;
    bool found = false;

    if (fp == NULL)
    {
        return false;
    }

    start_pos = ftell(fp);
    if (start_pos < 0)
    {
        start_pos = 0;
    }

    while (total_scanned < SD_PLAYER_SYNC_SCAN_TOTAL_BYTES)
    {
        size_t remain = SD_PLAYER_SYNC_SCAN_TOTAL_BYTES - total_scanned;
        size_t to_read = (remain < SD_PLAYER_SYNC_SCAN_CHUNK_SIZE) ? remain : SD_PLAYER_SYNC_SCAN_CHUNK_SIZE;
        size_t n = fread(scan_buf + carry_len, 1, to_read, fp);
        size_t valid_len = carry_len + n;

        if (valid_len < 4)
        {
            break;
        }

        for (size_t i = 0; i + 3 < valid_len; i++)
        {
            if (sd_player_is_valid_mp3_header(&scan_buf[i]))
            {
                long sync_pos = start_pos + (long)total_scanned - (long)carry_len + (long)i;
                
                ESP_LOGI("SD_PLAY", "[DEBUG] Found potential MP3 header at offset %ld", sync_pos);
                
                /* Check if this is a XING/INFO frame (VBR metadata) - skip it */
                if (sd_player_is_xing_info_frame(fp, sync_pos, &scan_buf[i]))
                {
                    ESP_LOGI("SD_PLAY", "[DEBUG] Skipping XING/INFO frame at offset %ld", sync_pos);
                    continue;
                }
                
                if (sd_player_validate_mp3_frame_chain(fp, sync_pos, 2))
                {
                    fseek(fp, sync_pos, SEEK_SET);
                    ESP_LOGI("SD_PLAY", "[DEBUG] Seek MP3 frame sync at offset %ld (audio)", sync_pos);
                    found = true;
                    break;
                }
            }
        }

        if (found)
        {
            break;
        }

        total_scanned += n;
        if (n < to_read)
        {
            break;
        }

        carry_len = (valid_len >= 3U) ? 3U : valid_len;
        memmove(scan_buf, scan_buf + valid_len - carry_len, carry_len);
    }

    if (!found)
    {
        fseek(fp, start_pos, SEEK_SET);
        ESP_LOGW("SD_PLAY", "[DEBUG] MP3 frame sync not found in first %u bytes", (unsigned)total_scanned);
    }

    return found;
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

    ESP_LOGI(sd_player_log_tag(ctx), "[DEBUG] Scanning for files at %s", SD_PLAYER_MOUNT_POINT);

    dir = opendir(SD_PLAYER_MOUNT_POINT);
    if (dir == NULL)
    {
        ESP_LOGE(sd_player_log_tag(ctx), "[DEBUG] Failed to open %s (errno=%d)", SD_PLAYER_MOUNT_POINT, errno);
        sd_player_set_status(ctx, "No USB drive");
        return;
    }

    ESP_LOGI(sd_player_log_tag(ctx), "[DEBUG] Directory opened successfully");

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
        ESP_LOGI(sd_player_log_tag(ctx), "[DEBUG] Added audio file #%d: %s", count + 1, ctx->sd_player_filenames[count]);
        count++;
    }

    closedir(dir);
    *ctx->sd_player_file_count = count;

    ESP_LOGI(sd_player_log_tag(ctx),
             "[DEBUG] Scan complete: found %d audio files out of %d total entries",
             count,
             total_entries);
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

    if (!sd_player_register_mp3_parser())
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

    if (!sd_player_context_valid(ctx) ||
        pcm_data == NULL ||
        pcm_size == 0 ||
        sample_rate_hz == 0 ||
        bits_per_sample != 16 ||
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
    size_t index;
    bool ok;

    if (stereo_buf == NULL)
    {
        sd_player_set_status(ctx, "Audio mem fail");
        return false;
    }

    for (index = 0; index < frame_count; index++)
    {
        stereo_buf[index * 2] = samples[index];
        stereo_buf[index * 2 + 1] = samples[index];
    }

    ok = audio_output_write_stereo_16(stereo_buf, frame_count, 1000) == ESP_OK;
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

    ESP_LOGI(sd_player_log_tag(ctx),
             "Open decoder for %s, free_heap=%u, largest_block=%u",
             filename,
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

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

    if (dec_type == ESP_AUDIO_SIMPLE_DEC_TYPE_MP3)
    {
        s_sd_mp3_header_log_count = 0;
        sd_player_skip_mp3_id3v2(fp);
        if (!sd_player_seek_mp3_frame_sync(fp) || !sd_player_mp3_profile_supported(fp))
        {
            sd_player_set_status(ctx, "MP3 profile unsupported");
            goto cleanup;
        }
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
        raw.frame_recover = (dec_type == ESP_AUDIO_SIMPLE_DEC_TYPE_MP3)
                    ? ESP_AUDIO_SIMPLE_DEC_RECOVERY_PLC
                    : ESP_AUDIO_SIMPLE_DEC_RECOVERY_NONE;

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
                        dec_info.sample_rate == 0 ||
                        dec_info.bits_per_sample != 16 ||
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
    audio_output_deinit();

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

    ESP_LOGI(sd_player_log_tag(ctx),
             "Playback cleanup done, free_heap=%u, largest_block=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

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

    ESP_LOGI(sd_player_log_tag(ctx), "Playing file %d: %s", file_index, sd_player_logic_filename(ctx, file_index));
    ESP_LOGI(sd_player_log_tag(ctx), "Task stack watermark(before)=%u", (unsigned)uxTaskGetStackHighWaterMark(NULL));

    (void)sd_player_play_file(ctx, file_index);

    ESP_LOGI(sd_player_log_tag(ctx), "Task stack watermark(after)=%u", (unsigned)uxTaskGetStackHighWaterMark(NULL));

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

void sd_player_logic_set_status(sd_player_logic_context_t *ctx, const char *text)
{
    sd_player_set_status(ctx, text);
}
