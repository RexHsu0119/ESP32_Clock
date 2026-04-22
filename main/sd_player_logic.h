#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "clock_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        const char *log_tag;
        bool *sd_player_mode;
        bool *sd_player_playing;
        bool *sd_player_connecting;
        int *sd_player_selected_file;
        int *sd_player_active_file;
        int *sd_player_file_count;
        volatile bool *sd_player_stop_requested;
        TaskHandle_t *sd_player_task_handle;
        char *sd_player_status_text;
        size_t sd_player_status_text_size;
        char (*sd_player_filenames)[SD_PLAYER_FILENAME_MAX_LEN];
        int sd_player_filenames_count; /* capacity (SD_PLAYER_MAX_FILES) */
        clock_panel_t *current_panel;
        clock_panel_t *last_clock_panel_before_overlay;
    } sd_player_logic_context_t;

    const char *sd_player_logic_filename(const sd_player_logic_context_t *ctx, int index);
    int sd_player_logic_file_count(const sd_player_logic_context_t *ctx);

    void sd_player_logic_enter_mode(sd_player_logic_context_t *ctx);
    void sd_player_logic_exit_mode(sd_player_logic_context_t *ctx);
    void sd_player_logic_move_file(sd_player_logic_context_t *ctx, int delta);
    void sd_player_logic_toggle_playback(sd_player_logic_context_t *ctx);
    void sd_player_logic_force_stop(sd_player_logic_context_t *ctx, const char *reason);
    void sd_player_logic_set_status(sd_player_logic_context_t *ctx, const char *text);

#ifdef __cplusplus
}
#endif
