#include "ui.h"

#include <stdio.h>

#include "esp_timer.h"
#include "display.h"
#include "wifi_portal.h"
#include "alarm_logic.h"

static SemaphoreHandle_t s_ui_mutex = NULL;
static ui_refresh_cb_t s_ui_refresh_cb = NULL;

/* boot overlay */
static lv_obj_t *s_boot_overlay = NULL;
static lv_obj_t *s_boot_overlay_title = NULL;
static lv_obj_t *s_boot_overlay_line1 = NULL;
static lv_obj_t *s_boot_overlay_line2 = NULL;
static lv_obj_t *s_boot_overlay_line3 = NULL;
static lv_obj_t *s_boot_overlay_line4 = NULL;

/* alarm overlay */
static lv_obj_t *s_alarm_overlay = NULL;
static lv_obj_t *s_alarm_title_label = NULL;
static lv_obj_t *s_alarm_help_label = NULL;
static lv_obj_t *s_alarm_list_row_bg[ALARM_SLOT_COUNT] = {0};
static lv_obj_t *s_alarm_list_labels[ALARM_SLOT_COUNT] = {0};
static lv_obj_t *s_alarm_list_time_labels[ALARM_SLOT_COUNT] = {0};
static lv_obj_t *s_alarm_list_state_labels[ALARM_SLOT_COUNT] = {0};
static lv_obj_t *s_alarm_hour_label = NULL;
static lv_obj_t *s_alarm_colon_label = NULL;
static lv_obj_t *s_alarm_minute_label = NULL;
static lv_obj_t *s_alarm_status_prefix_label = NULL;
static lv_obj_t *s_alarm_status_label = NULL;

/* timer overlay */
static lv_obj_t *s_timer_overlay = NULL;
static lv_obj_t *s_timer_title_label = NULL;
static lv_obj_t *s_timer_hour_label = NULL;
static lv_obj_t *s_timer_colon1_label = NULL;
static lv_obj_t *s_timer_minute_label = NULL;
static lv_obj_t *s_timer_colon2_label = NULL;
static lv_obj_t *s_timer_second_label = NULL;
static lv_obj_t *s_timer_status_label = NULL;
static lv_obj_t *s_timer_help_label = NULL;

/* stopwatch overlay */
static lv_obj_t *s_stopwatch_overlay = NULL;
static lv_obj_t *s_stopwatch_title_label = NULL;
static lv_obj_t *s_stopwatch_hour_label = NULL;
static lv_obj_t *s_stopwatch_colon1_label = NULL;
static lv_obj_t *s_stopwatch_minute_label = NULL;
static lv_obj_t *s_stopwatch_colon2_label = NULL;
static lv_obj_t *s_stopwatch_second_label = NULL;
static lv_obj_t *s_stopwatch_colon3_label = NULL;
static lv_obj_t *s_stopwatch_centisecond_label = NULL;
static lv_obj_t *s_stopwatch_status_label = NULL;
static lv_obj_t *s_stopwatch_help_label = NULL;

/* menu overlay */
static lv_obj_t *s_menu_overlay = NULL;
static lv_obj_t *s_menu_title_label = NULL;
static lv_obj_t *s_menu_help_label = NULL;
static lv_obj_t *s_menu_row_bg[4] = {0};
static lv_obj_t *s_menu_item_labels[4] = {0};

/* confirm overlay */
static lv_obj_t *s_confirm_overlay = NULL;
static lv_obj_t *s_confirm_title_label = NULL;
static lv_obj_t *s_confirm_msg_label = NULL;
static lv_obj_t *s_confirm_no_label = NULL;
static lv_obj_t *s_confirm_yes_label = NULL;
static lv_obj_t *s_confirm_help_label = NULL;

/* portal ui */
static lv_obj_t *s_portal_container = NULL;
static lv_obj_t *s_portal_title_label = NULL;
static lv_obj_t *s_portal_line1_label = NULL;
static lv_obj_t *s_portal_line2_label = NULL;
static lv_obj_t *s_portal_line3_label = NULL;
static lv_obj_t *s_portal_line4_label = NULL;
static lv_obj_t *s_portal_footer_label = NULL;

#define UI_SETTING_BLINK_PERIOD_US 500000LL

static const char *alarm_list_state_text(const alarm_config_t *alarm)
{
    if (alarm == NULL || !alarm->enabled)
    {
        return "OFF";
    }

    return alarm_repeat_text(alarm->repeat);
}

static void timer_format_hms_from_seconds(int total_seconds, int *hours, int *minutes, int *seconds)
{
    if (total_seconds < 0)
    {
        total_seconds = 0;
    }

    if (hours != NULL)
    {
        *hours = total_seconds / 3600;
    }
    if (minutes != NULL)
    {
        *minutes = (total_seconds % 3600) / 60;
    }
    if (seconds != NULL)
    {
        *seconds = total_seconds % 60;
    }
}

static void stopwatch_format_from_elapsed_ms(int64_t elapsed_ms,
                                             int *hours,
                                             int *minutes,
                                             int *seconds,
                                             int *centiseconds)
{
    int64_t total_seconds;

    if (elapsed_ms < 0)
    {
        elapsed_ms = 0;
    }

    total_seconds = elapsed_ms / 1000LL;

    if (hours != NULL)
    {
        *hours = (int)(total_seconds / 3600LL);
    }
    if (minutes != NULL)
    {
        *minutes = (int)((total_seconds % 3600LL) / 60LL);
    }
    if (seconds != NULL)
    {
        *seconds = (int)(total_seconds % 60LL);
    }
    if (centiseconds != NULL)
    {
        *centiseconds = (int)((elapsed_ms % 1000LL) / 10LL);
    }
}

void ui_init(SemaphoreHandle_t mutex, ui_refresh_cb_t refresh_cb)
{
    s_ui_mutex = mutex;
    s_ui_refresh_cb = refresh_cb;
}

bool ui_lock(TickType_t timeout_ticks)
{
    if (s_ui_mutex == NULL)
    {
        return false;
    }

    return (xSemaphoreTake(s_ui_mutex, timeout_ticks) == pdTRUE);
}

void ui_unlock(void)
{
    if (s_ui_mutex != NULL)
    {
        xSemaphoreGive(s_ui_mutex);
    }
}

void ui_refresh(void)
{
    if (s_ui_refresh_cb != NULL)
    {
        s_ui_refresh_cb();
    }
}

SemaphoreHandle_t ui_get_mutex(void)
{
    return s_ui_mutex;
}

/* =========================
 * Boot overlay
 * ========================= */
void ui_create_boot_overlay(lv_obj_t *scr)
{
    s_boot_overlay = lv_obj_create(scr);
    lv_obj_set_size(s_boot_overlay, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_center(s_boot_overlay);
    lv_obj_set_style_bg_color(s_boot_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_boot_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_boot_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_boot_overlay, 0, 0);
    lv_obj_clear_flag(s_boot_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_boot_overlay, LV_OBJ_FLAG_HIDDEN);

    s_boot_overlay_title = lv_label_create(s_boot_overlay);
    lv_obj_set_width(s_boot_overlay_title, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_boot_overlay_title, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_boot_overlay_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_boot_overlay_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_boot_overlay_title, &lv_font_montserrat_18, 0);
    lv_label_set_text(s_boot_overlay_title, "FORCE SETUP");
    lv_obj_set_pos(s_boot_overlay_title, 0, 8);

    s_boot_overlay_line1 = lv_label_create(s_boot_overlay);
    lv_obj_set_width(s_boot_overlay_line1, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_boot_overlay_line1, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_boot_overlay_line1, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_boot_overlay_line1, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(s_boot_overlay_line1, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_boot_overlay_line1, "Entering setup mode");
    lv_obj_set_pos(s_boot_overlay_line1, 0, 36);

    s_boot_overlay_line2 = lv_label_create(s_boot_overlay);
    lv_obj_set_width(s_boot_overlay_line2, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_boot_overlay_line2, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_boot_overlay_line2, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_boot_overlay_line2, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(s_boot_overlay_line2, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_boot_overlay_line2, "AP: ClockSetup");
    lv_obj_set_pos(s_boot_overlay_line2, 0, 60);

    s_boot_overlay_line3 = lv_label_create(s_boot_overlay);
    lv_obj_set_width(s_boot_overlay_line3, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_boot_overlay_line3, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_boot_overlay_line3, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_boot_overlay_line3, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(s_boot_overlay_line3, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_boot_overlay_line3, "IP: 192.168.4.1");
    lv_obj_set_pos(s_boot_overlay_line3, 0, 76);

    s_boot_overlay_line4 = lv_label_create(s_boot_overlay);
    lv_obj_set_width(s_boot_overlay_line4, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_boot_overlay_line4, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_boot_overlay_line4, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_boot_overlay_line4, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(s_boot_overlay_line4, &lv_font_montserrat_10, 0);
    lv_label_set_text(s_boot_overlay_line4, "Open on phone");
    lv_obj_set_pos(s_boot_overlay_line4, 0, 104);
}

void ui_show_boot_overlay(int hint)
{
    if (s_boot_overlay == NULL)
    {
        return;
    }

    switch (hint)
    {
    case 1: /* BOOT_HINT_FORCE_SETUP */
        lv_label_set_text(s_boot_overlay_title, "FORCE SETUP");
        lv_label_set_text(s_boot_overlay_line1, "Entering setup mode");
        lv_label_set_text(s_boot_overlay_line2, "AP: ClockSetup");
        lv_label_set_text(s_boot_overlay_line3, "IP: 192.168.4.1");
        if (s_boot_overlay_line4 != NULL)
        {
            lv_label_set_text(s_boot_overlay_line4, "Open on phone");
        }
        break;

    case 2: /* BOOT_HINT_CLEAR_WIFI */
        lv_label_set_text(s_boot_overlay_title, "CLEAR WIFI");
        lv_label_set_text(s_boot_overlay_line1, "Credentials erased");
        lv_label_set_text(s_boot_overlay_line2, "AP: ClockSetup");
        lv_label_set_text(s_boot_overlay_line3, "IP: 192.168.4.1");
        if (s_boot_overlay_line4 != NULL)
        {
            lv_label_set_text(s_boot_overlay_line4, "Open on phone");
        }
        break;

    default:
        lv_obj_add_flag(s_boot_overlay, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_move_foreground(s_boot_overlay);
    lv_obj_clear_flag(s_boot_overlay, LV_OBJ_FLAG_HIDDEN);
}

void ui_hide_boot_overlay(void)
{
    if (s_boot_overlay != NULL)
    {
        lv_obj_add_flag(s_boot_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

/* =========================
 * Alarm overlay
 * ========================= */
void ui_create_alarm_overlay(lv_obj_t *scr)
{
    s_alarm_overlay = lv_obj_create(scr);
    lv_obj_set_size(s_alarm_overlay, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_center(s_alarm_overlay);
    lv_obj_set_style_bg_color(s_alarm_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_alarm_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_alarm_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_alarm_overlay, 0, 0);
    lv_obj_clear_flag(s_alarm_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_alarm_overlay, LV_OBJ_FLAG_HIDDEN);

    s_alarm_title_label = lv_label_create(s_alarm_overlay);
    lv_obj_set_width(s_alarm_title_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_alarm_title_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_alarm_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_alarm_title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_alarm_title_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_alarm_title_label, "ALARM SET");
    lv_obj_set_pos(s_alarm_title_label, 0, 4);

    for (int i = 0; i < ALARM_SLOT_COUNT; i++)
    {
        s_alarm_list_row_bg[i] = lv_obj_create(s_alarm_overlay);
        lv_obj_set_size(s_alarm_list_row_bg[i], DISPLAY_WIDTH - 4, 26);
        lv_obj_set_pos(s_alarm_list_row_bg[i], 2, 23 + i * 27);
        lv_obj_set_style_bg_color(s_alarm_list_row_bg[i], lv_color_hex(0x00FFCC), 0);
        lv_obj_set_style_bg_opa(s_alarm_list_row_bg[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_alarm_list_row_bg[i], 0, 0);
        lv_obj_set_style_radius(s_alarm_list_row_bg[i], 4, 0);
        lv_obj_set_style_pad_all(s_alarm_list_row_bg[i], 0, 0);
        lv_obj_clear_flag(s_alarm_list_row_bg[i], LV_OBJ_FLAG_SCROLLABLE);

        s_alarm_list_labels[i] = lv_label_create(s_alarm_overlay);
        lv_obj_set_width(s_alarm_list_labels[i], 30);
        lv_label_set_long_mode(s_alarm_list_labels[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(s_alarm_list_labels[i], LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_color(s_alarm_list_labels[i], lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(s_alarm_list_labels[i], &lv_font_montserrat_12, 0);
        lv_label_set_text(s_alarm_list_labels[i], "");
        lv_obj_set_pos(s_alarm_list_labels[i], 8, 30 + i * 27);

        s_alarm_list_time_labels[i] = lv_label_create(s_alarm_overlay);
        lv_obj_set_width(s_alarm_list_time_labels[i], 70);
        lv_label_set_long_mode(s_alarm_list_time_labels[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(s_alarm_list_time_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(s_alarm_list_time_labels[i], lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(s_alarm_list_time_labels[i], &lv_font_montserrat_18, 0);
        lv_label_set_text(s_alarm_list_time_labels[i], "");
        lv_obj_set_pos(s_alarm_list_time_labels[i], 36, 27 + i * 27);

        s_alarm_list_state_labels[i] = lv_label_create(s_alarm_overlay);
        lv_obj_set_width(s_alarm_list_state_labels[i], 42);
        lv_label_set_long_mode(s_alarm_list_state_labels[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(s_alarm_list_state_labels[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(s_alarm_list_state_labels[i], lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_text_font(s_alarm_list_state_labels[i], &lv_font_montserrat_12, 0);
        lv_label_set_text(s_alarm_list_state_labels[i], "");
        lv_obj_set_pos(s_alarm_list_state_labels[i], 112, 30 + i * 27);
    }

    s_alarm_hour_label = lv_label_create(s_alarm_overlay);
    lv_obj_set_width(s_alarm_hour_label, 42);
    lv_obj_set_style_text_align(s_alarm_hour_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_alarm_hour_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(s_alarm_hour_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_alarm_hour_label, "07");
    lv_obj_set_pos(s_alarm_hour_label, 30, 28);

    s_alarm_colon_label = lv_label_create(s_alarm_overlay);
    lv_obj_set_width(s_alarm_colon_label, 16);
    lv_obj_set_style_text_align(s_alarm_colon_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_alarm_colon_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(s_alarm_colon_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_alarm_colon_label, ":");
    lv_obj_set_pos(s_alarm_colon_label, 72, 28);

    s_alarm_minute_label = lv_label_create(s_alarm_overlay);
    lv_obj_set_width(s_alarm_minute_label, 42);
    lv_obj_set_style_text_align(s_alarm_minute_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_alarm_minute_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(s_alarm_minute_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_alarm_minute_label, "00");
    lv_obj_set_pos(s_alarm_minute_label, 88, 28);

    s_alarm_status_prefix_label = lv_label_create(s_alarm_overlay);
    lv_obj_set_style_text_color(s_alarm_status_prefix_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(s_alarm_status_prefix_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_alarm_status_prefix_label, "Status:");
    lv_obj_set_pos(s_alarm_status_prefix_label, 24, 70);

    s_alarm_status_label = lv_label_create(s_alarm_overlay);
    lv_obj_set_width(s_alarm_status_label, 56);
    lv_obj_set_style_text_align(s_alarm_status_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(s_alarm_status_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(s_alarm_status_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_alarm_status_label, "OFF");
    lv_obj_set_pos(s_alarm_status_label, 92, 70);

    s_alarm_help_label = lv_label_create(s_alarm_overlay);
    lv_obj_set_width(s_alarm_help_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_alarm_help_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_alarm_help_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_alarm_help_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(s_alarm_help_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(s_alarm_help_label, "UP/DN adj\nCENTER next/save");
    lv_obj_set_pos(s_alarm_help_label, 0, 104);
}

void ui_alarm_overlay_move_foreground(void)
{
    if (s_alarm_overlay != NULL)
    {
        lv_obj_move_foreground(s_alarm_overlay);
    }
}

void ui_create_timer_overlay(lv_obj_t *scr)
{
    s_timer_overlay = lv_obj_create(scr);
    lv_obj_set_size(s_timer_overlay, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_center(s_timer_overlay);
    lv_obj_set_style_bg_color(s_timer_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_timer_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_timer_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_timer_overlay, 0, 0);
    lv_obj_clear_flag(s_timer_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_timer_overlay, LV_OBJ_FLAG_HIDDEN);

    s_timer_title_label = lv_label_create(s_timer_overlay);
    lv_obj_set_width(s_timer_title_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_timer_title_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_timer_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_timer_title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_timer_title_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_timer_title_label, "TIMER");
    lv_obj_set_pos(s_timer_title_label, 0, 8);

    s_timer_hour_label = lv_label_create(s_timer_overlay);
    lv_obj_set_width(s_timer_hour_label, 32);
    lv_obj_set_style_text_align(s_timer_hour_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_timer_hour_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(s_timer_hour_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_timer_hour_label, "00");
    lv_obj_set_pos(s_timer_hour_label, 18, 34);

    s_timer_colon1_label = lv_label_create(s_timer_overlay);
    lv_obj_set_width(s_timer_colon1_label, 10);
    lv_obj_set_style_text_align(s_timer_colon1_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_timer_colon1_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(s_timer_colon1_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_timer_colon1_label, ":");
    lv_obj_set_pos(s_timer_colon1_label, 50, 34);

    s_timer_minute_label = lv_label_create(s_timer_overlay);
    lv_obj_set_width(s_timer_minute_label, 32);
    lv_obj_set_style_text_align(s_timer_minute_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_timer_minute_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(s_timer_minute_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_timer_minute_label, "00");
    lv_obj_set_pos(s_timer_minute_label, 60, 34);

    s_timer_colon2_label = lv_label_create(s_timer_overlay);
    lv_obj_set_width(s_timer_colon2_label, 10);
    lv_obj_set_style_text_align(s_timer_colon2_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_timer_colon2_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(s_timer_colon2_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_timer_colon2_label, ":");
    lv_obj_set_pos(s_timer_colon2_label, 92, 34);

    s_timer_second_label = lv_label_create(s_timer_overlay);
    lv_obj_set_width(s_timer_second_label, 32);
    lv_obj_set_style_text_align(s_timer_second_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_timer_second_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(s_timer_second_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_timer_second_label, "00");
    lv_obj_set_pos(s_timer_second_label, 102, 34);

    s_timer_status_label = lv_label_create(s_timer_overlay);
    lv_obj_set_width(s_timer_status_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_timer_status_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_timer_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_timer_status_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(s_timer_status_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_timer_status_label, "SET");
    lv_obj_set_pos(s_timer_status_label, 0, 72);

    s_timer_help_label = lv_label_create(s_timer_overlay);
    lv_obj_set_width(s_timer_help_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_timer_help_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_timer_help_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_timer_help_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(s_timer_help_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(s_timer_help_label, "UP/DN adj  U+D clear\nCENTER next  LONG start");
    lv_obj_set_pos(s_timer_help_label, 0, 102);
}

void ui_update_timer_overlay_locked(bool timer_mode,
                                    int timer_state,
                                    int timer_field,
                                    int timer_hours,
                                    int timer_minutes,
                                    int timer_seconds,
                                    int remaining_seconds)
{
    bool blink_on;
    bool show_hour = true;
    bool show_minute = true;
    bool show_second = true;
    int display_hours = timer_hours;
    int display_minutes = timer_minutes;
    int display_seconds = timer_seconds;
    lv_color_t time_color = lv_color_hex(0xFFFFFF);
    lv_color_t colon_color = lv_color_hex(0xFFFFFF);
    char hour_buf[3] = "00";
    char minute_buf[3] = "00";
    char second_buf[3] = "00";

    if (s_timer_overlay == NULL)
    {
        return;
    }

    if (!timer_mode)
    {
        lv_obj_add_flag(s_timer_overlay, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (timer_state == TIMER_STATE_RUNNING || timer_state == TIMER_STATE_PAUSED || timer_state == TIMER_STATE_DONE)
    {
        timer_format_hms_from_seconds(remaining_seconds, &display_hours, &display_minutes, &display_seconds);
    }

    blink_on = ((esp_timer_get_time() / UI_SETTING_BLINK_PERIOD_US) % 2) == 0;
    if (timer_state == TIMER_STATE_SET && !blink_on)
    {
        switch (timer_field)
        {
        case TIMER_FIELD_HOUR:
            show_hour = false;
            break;
        case TIMER_FIELD_MINUTE:
            show_minute = false;
            break;
        case TIMER_FIELD_SECOND:
            show_second = false;
            break;
        }
    }

    if (timer_state == TIMER_STATE_SET || timer_state == TIMER_STATE_IDLE)
    {
        time_color = lv_color_hex(0xFF0000);
        colon_color = lv_color_hex(0xFF0000);
    }
    else if (timer_state == TIMER_STATE_DONE && blink_on)
    {
        time_color = lv_color_hex(0xFFE082);
        colon_color = lv_color_hex(0xFFB300);
    }

    lv_obj_set_style_bg_color(s_timer_overlay,
                              (timer_state == TIMER_STATE_DONE && blink_on)
                                  ? lv_color_hex(0x500000)
                                  : lv_color_hex(0x000000),
                              0);

    if (show_hour)
    {
        snprintf(hour_buf, sizeof(hour_buf), "%02d", display_hours);
    }
    else
    {
        hour_buf[0] = ' ';
        hour_buf[1] = ' ';
        hour_buf[2] = '\0';
    }

    if (show_minute)
    {
        snprintf(minute_buf, sizeof(minute_buf), "%02d", display_minutes);
    }
    else
    {
        minute_buf[0] = ' ';
        minute_buf[1] = ' ';
        minute_buf[2] = '\0';
    }

    if (show_second)
    {
        snprintf(second_buf, sizeof(second_buf), "%02d", display_seconds);
    }
    else
    {
        second_buf[0] = ' ';
        second_buf[1] = ' ';
        second_buf[2] = '\0';
    }

    if (s_timer_title_label != NULL)
    {
        lv_label_set_text(s_timer_title_label, "TIMER");
    }

    if (s_timer_hour_label != NULL)
    {
        lv_obj_set_style_text_color(s_timer_hour_label, time_color, 0);
        lv_label_set_text(s_timer_hour_label, hour_buf);
    }

    if (s_timer_colon1_label != NULL)
    {
        lv_obj_set_style_text_color(s_timer_colon1_label, colon_color, 0);
        lv_label_set_text(s_timer_colon1_label, ":");
    }

    if (s_timer_minute_label != NULL)
    {
        lv_obj_set_style_text_color(s_timer_minute_label, time_color, 0);
        lv_label_set_text(s_timer_minute_label, minute_buf);
    }

    if (s_timer_colon2_label != NULL)
    {
        lv_obj_set_style_text_color(s_timer_colon2_label, colon_color, 0);
        lv_label_set_text(s_timer_colon2_label, ":");
    }

    if (s_timer_second_label != NULL)
    {
        lv_obj_set_style_text_color(s_timer_second_label, time_color, 0);
        lv_label_set_text(s_timer_second_label, second_buf);
    }

    if (s_timer_status_label != NULL)
    {
        lv_obj_set_style_text_color(s_timer_status_label,
                                    (timer_state == TIMER_STATE_DONE && blink_on)
                                        ? lv_color_hex(0xFFB300)
                                        : lv_color_hex(0x00FFCC),
                                    0);
        switch (timer_state)
        {
        case TIMER_STATE_RUNNING:
            lv_label_set_text(s_timer_status_label, "RUNNING");
            break;
        case TIMER_STATE_PAUSED:
            lv_label_set_text(s_timer_status_label, "PAUSED");
            break;
        case TIMER_STATE_DONE:
            lv_label_set_text(s_timer_status_label, "DONE");
            break;
        case TIMER_STATE_SET:
        case TIMER_STATE_IDLE:
        default:
            lv_label_set_text(s_timer_status_label, "SET");
            break;
        }
    }

    if (s_timer_help_label != NULL)
    {
        switch (timer_state)
        {
        case TIMER_STATE_RUNNING:
            lv_label_set_text(s_timer_help_label, "CENTER pause  U+D cancel\nLONG back");
            break;
        case TIMER_STATE_PAUSED:
            lv_label_set_text(s_timer_help_label, "CENTER resume  U+D cancel\nLONG back");
            break;
        case TIMER_STATE_DONE:
            lv_label_set_text(s_timer_help_label, "Press any key to close");
            break;
        case TIMER_STATE_SET:
        case TIMER_STATE_IDLE:
        default:
            lv_label_set_text(s_timer_help_label, "UP/DN adj  U+D clear\nCENTER next  LONG start");
            break;
        }
    }

    lv_obj_clear_flag(s_timer_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_timer_overlay);
}

void ui_create_stopwatch_overlay(lv_obj_t *scr)
{
    s_stopwatch_overlay = lv_obj_create(scr);
    lv_obj_set_size(s_stopwatch_overlay, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_center(s_stopwatch_overlay);
    lv_obj_set_style_bg_color(s_stopwatch_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_stopwatch_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_stopwatch_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_stopwatch_overlay, 0, 0);
    lv_obj_clear_flag(s_stopwatch_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_stopwatch_overlay, LV_OBJ_FLAG_HIDDEN);

    s_stopwatch_title_label = lv_label_create(s_stopwatch_overlay);
    lv_obj_set_width(s_stopwatch_title_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_stopwatch_title_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_stopwatch_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_stopwatch_title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_stopwatch_title_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_stopwatch_title_label, "STOPWATCH");
    lv_obj_set_pos(s_stopwatch_title_label, 0, 8);

    s_stopwatch_hour_label = lv_label_create(s_stopwatch_overlay);
    lv_obj_set_width(s_stopwatch_hour_label, 34);
    lv_label_set_long_mode(s_stopwatch_hour_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_stopwatch_hour_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_stopwatch_hour_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(s_stopwatch_hour_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_stopwatch_hour_label, "00");
    lv_obj_set_pos(s_stopwatch_hour_label, 0, 32);

    s_stopwatch_colon1_label = lv_label_create(s_stopwatch_overlay);
    lv_obj_set_width(s_stopwatch_colon1_label, 12);
    lv_label_set_long_mode(s_stopwatch_colon1_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_stopwatch_colon1_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_stopwatch_colon1_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(s_stopwatch_colon1_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_stopwatch_colon1_label, ":");
    lv_obj_set_pos(s_stopwatch_colon1_label, 31, 32);

    s_stopwatch_minute_label = lv_label_create(s_stopwatch_overlay);
    lv_obj_set_width(s_stopwatch_minute_label, 34);
    lv_label_set_long_mode(s_stopwatch_minute_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_stopwatch_minute_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_stopwatch_minute_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(s_stopwatch_minute_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_stopwatch_minute_label, "00");
    lv_obj_set_pos(s_stopwatch_minute_label, 43, 32);

    s_stopwatch_colon2_label = lv_label_create(s_stopwatch_overlay);
    lv_obj_set_width(s_stopwatch_colon2_label, 12);
    lv_label_set_long_mode(s_stopwatch_colon2_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_stopwatch_colon2_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_stopwatch_colon2_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(s_stopwatch_colon2_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_stopwatch_colon2_label, ":");
    lv_obj_set_pos(s_stopwatch_colon2_label, 74, 32);

    s_stopwatch_second_label = lv_label_create(s_stopwatch_overlay);
    lv_obj_set_width(s_stopwatch_second_label, 34);
    lv_label_set_long_mode(s_stopwatch_second_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_stopwatch_second_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_stopwatch_second_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(s_stopwatch_second_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_stopwatch_second_label, "00");
    lv_obj_set_pos(s_stopwatch_second_label, 86, 32);

    s_stopwatch_colon3_label = lv_label_create(s_stopwatch_overlay);
    lv_obj_set_width(s_stopwatch_colon3_label, 10);
    lv_label_set_long_mode(s_stopwatch_colon3_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_stopwatch_colon3_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_stopwatch_colon3_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(s_stopwatch_colon3_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_stopwatch_colon3_label, ".");
    lv_obj_set_pos(s_stopwatch_colon3_label, 120, 42);

    s_stopwatch_centisecond_label = lv_label_create(s_stopwatch_overlay);
    lv_obj_set_width(s_stopwatch_centisecond_label, 28);
    lv_label_set_long_mode(s_stopwatch_centisecond_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_stopwatch_centisecond_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_stopwatch_centisecond_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(s_stopwatch_centisecond_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_stopwatch_centisecond_label, "00");
    lv_obj_set_pos(s_stopwatch_centisecond_label, 130, 42);

    s_stopwatch_status_label = lv_label_create(s_stopwatch_overlay);
    lv_obj_set_width(s_stopwatch_status_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_stopwatch_status_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_stopwatch_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_stopwatch_status_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(s_stopwatch_status_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_stopwatch_status_label, "READY");
    lv_obj_set_pos(s_stopwatch_status_label, 0, 74);

    s_stopwatch_help_label = lv_label_create(s_stopwatch_overlay);
    lv_obj_set_width(s_stopwatch_help_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_stopwatch_help_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_stopwatch_help_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_stopwatch_help_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(s_stopwatch_help_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(s_stopwatch_help_label, "CENTER start  U+D reset\nLONG back");
    lv_obj_set_pos(s_stopwatch_help_label, 0, 100);
}

void ui_update_stopwatch_overlay_locked(bool stopwatch_mode,
                                        int stopwatch_state,
                                        int64_t elapsed_ms)
{
    int display_hours = 0;
    int display_minutes = 0;
    int display_seconds = 0;
    int display_centiseconds = 0;
    lv_color_t time_color = lv_color_hex(0x00FFCC);
    lv_color_t status_color = lv_color_hex(0x00FFCC);

    if (s_stopwatch_overlay == NULL)
    {
        return;
    }

    if (!stopwatch_mode)
    {
        lv_obj_add_flag(s_stopwatch_overlay, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    stopwatch_format_from_elapsed_ms(elapsed_ms,
                                     &display_hours,
                                     &display_minutes,
                                     &display_seconds,
                                     &display_centiseconds);

    if (stopwatch_state == STOPWATCH_STATE_PAUSED)
    {
        time_color = lv_color_hex(0xFFE082);
        status_color = lv_color_hex(0xFFB300);
    }
    else if (stopwatch_state == STOPWATCH_STATE_IDLE)
    {
        time_color = lv_color_hex(0xFFFFFF);
        status_color = lv_color_hex(0xAAAAAA);
    }

    if (s_stopwatch_title_label != NULL)
    {
        lv_label_set_text(s_stopwatch_title_label, "STOPWATCH");
    }

    if (s_stopwatch_hour_label != NULL)
    {
        lv_obj_set_style_text_color(s_stopwatch_hour_label, time_color, 0);
        lv_label_set_text_fmt(s_stopwatch_hour_label, "%02d", display_hours % 100);
    }

    if (s_stopwatch_colon1_label != NULL)
    {
        lv_obj_set_style_text_color(s_stopwatch_colon1_label, time_color, 0);
        lv_label_set_text(s_stopwatch_colon1_label, ":");
    }

    if (s_stopwatch_minute_label != NULL)
    {
        lv_obj_set_style_text_color(s_stopwatch_minute_label, time_color, 0);
        lv_label_set_text_fmt(s_stopwatch_minute_label, "%02d", display_minutes);
    }

    if (s_stopwatch_colon2_label != NULL)
    {
        lv_obj_set_style_text_color(s_stopwatch_colon2_label, time_color, 0);
        lv_label_set_text(s_stopwatch_colon2_label, ":");
    }

    if (s_stopwatch_second_label != NULL)
    {
        lv_obj_set_style_text_color(s_stopwatch_second_label, time_color, 0);
        lv_label_set_text_fmt(s_stopwatch_second_label, "%02d", display_seconds);
    }

    if (s_stopwatch_colon3_label != NULL)
    {
        lv_obj_set_style_text_color(s_stopwatch_colon3_label, time_color, 0);
        lv_label_set_text(s_stopwatch_colon3_label, ".");
    }

    if (s_stopwatch_centisecond_label != NULL)
    {
        lv_obj_set_style_text_color(s_stopwatch_centisecond_label, time_color, 0);
        lv_label_set_text_fmt(s_stopwatch_centisecond_label, "%02d", display_centiseconds);
    }

    if (s_stopwatch_status_label != NULL)
    {
        lv_obj_set_style_text_color(s_stopwatch_status_label, status_color, 0);
        switch (stopwatch_state)
        {
        case STOPWATCH_STATE_RUNNING:
            lv_label_set_text(s_stopwatch_status_label, "RUNNING");
            break;
        case STOPWATCH_STATE_PAUSED:
            lv_label_set_text(s_stopwatch_status_label, "PAUSED");
            break;
        case STOPWATCH_STATE_IDLE:
        default:
            lv_label_set_text(s_stopwatch_status_label, "READY");
            break;
        }
    }

    if (s_stopwatch_help_label != NULL)
    {
        if (stopwatch_state == STOPWATCH_STATE_RUNNING)
        {
            lv_label_set_text(s_stopwatch_help_label, "CENTER pause\nLONG back");
        }
        else
        {
            lv_label_set_text(s_stopwatch_help_label, "CENTER start  U+D reset\nLONG back");
        }
    }

    lv_obj_clear_flag(s_stopwatch_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_stopwatch_overlay);
}

void ui_update_alarm_overlay_locked(bool alarm_setting_mode,
                                    int alarm_ui_mode,
                                    int selected_alarm_index,
                                    const alarm_config_t *alarms,
                                    int alarm_count,
                                    int alarm_field,
                                    bool enabled,
                                    int repeat,
                                    int hour,
                                    int minute)
{
    if (s_alarm_overlay == NULL)
    {
        return;
    }

    if (!alarm_setting_mode)
    {
        lv_obj_add_flag(s_alarm_overlay, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    bool list_mode = (alarm_ui_mode == ALARM_UI_LIST);

    for (int i = 0; i < ALARM_SLOT_COUNT; i++)
    {
        if (s_alarm_list_labels[i] == NULL)
        {
            continue;
        }

        if (list_mode && i < alarm_count && alarms != NULL)
        {
            bool is_selected = (i == selected_alarm_index);
            lv_label_set_text_fmt(s_alarm_list_labels[i], "A%d", i + 1);
            lv_label_set_text_fmt(s_alarm_list_time_labels[i], "%02d:%02d", alarms[i].hour, alarms[i].minute);
            lv_label_set_text(s_alarm_list_state_labels[i], alarm_list_state_text(&alarms[i]));

            lv_obj_set_style_bg_opa(s_alarm_list_row_bg[i],
                                    is_selected ? LV_OPA_COVER : LV_OPA_TRANSP,
                                    0);

            lv_obj_set_style_text_color(s_alarm_list_labels[i],
                                        is_selected ? lv_color_hex(0x101010) : lv_color_hex(0xFFFFFF),
                                        0);
            lv_obj_set_style_text_font(s_alarm_list_labels[i], &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(s_alarm_list_time_labels[i],
                                        is_selected ? lv_color_hex(0x101010) : lv_color_hex(0xFFFFFF),
                                        0);
            lv_obj_set_style_text_font(s_alarm_list_time_labels[i], &lv_font_montserrat_18, 0);
            lv_obj_set_style_text_color(s_alarm_list_state_labels[i],
                                        is_selected ? lv_color_hex(0x101010) : lv_color_hex(0xAAAAAA),
                                        0);
            lv_obj_set_style_text_font(s_alarm_list_state_labels[i], &lv_font_montserrat_12, 0);
            lv_obj_clear_flag(s_alarm_list_row_bg[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_alarm_list_labels[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_alarm_list_time_labels[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_alarm_list_state_labels[i], LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(s_alarm_list_row_bg[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_alarm_list_labels[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_alarm_list_time_labels[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_alarm_list_state_labels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    lv_obj_t *edit_objs[] = {
        s_alarm_hour_label,
        s_alarm_colon_label,
        s_alarm_minute_label,
        s_alarm_status_prefix_label,
        s_alarm_status_label,
    };

    for (unsigned int i = 0; i < (sizeof(edit_objs) / sizeof(edit_objs[0])); i++)
    {
        if (edit_objs[i] == NULL)
        {
            continue;
        }

        if (list_mode)
        {
            lv_obj_add_flag(edit_objs[i], LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_clear_flag(edit_objs[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (list_mode)
    {
        if (s_alarm_title_label != NULL)
        {
            lv_label_set_text(s_alarm_title_label, "ALARM LIST");
        }

        if (s_alarm_help_label != NULL)
        {
            lv_label_set_text(s_alarm_help_label, "UP/DN sel  U+D state\nCENTER edit  LONG back");
        }

        lv_obj_clear_flag(s_alarm_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_alarm_overlay);
        return;
    }

    bool blink_on = ((esp_timer_get_time() / UI_SETTING_BLINK_PERIOD_US) % 2) == 0;

    bool show_status = true;
    bool show_hour = true;
    bool show_minute = true;

    if (!blink_on)
    {
        switch (alarm_field)
        {
        case 0:
            show_status = false;
            break;
        case 1:
            show_hour = false;
            break;
        case 2:
            show_minute = false;
            break;
        default:
            break;
        }
    }

    if (s_alarm_title_label != NULL)
    {
        lv_label_set_text_fmt(s_alarm_title_label, "ALARM A%d", selected_alarm_index + 1);
    }

    if (s_alarm_hour_label != NULL)
    {
        if (show_hour)
            lv_label_set_text_fmt(s_alarm_hour_label, "%02d", hour);
        else
            lv_label_set_text(s_alarm_hour_label, "  ");
    }

    if (s_alarm_colon_label != NULL)
    {
        lv_label_set_text(s_alarm_colon_label, ":");
    }

    if (s_alarm_minute_label != NULL)
    {
        if (show_minute)
            lv_label_set_text_fmt(s_alarm_minute_label, "%02d", minute);
        else
            lv_label_set_text(s_alarm_minute_label, "  ");
    }

    if (s_alarm_status_label != NULL)
    {
        if (show_status)
            lv_label_set_text(s_alarm_status_label, enabled ? alarm_repeat_text(repeat) : "OFF");
        else
            lv_label_set_text(s_alarm_status_label, "     ");
    }

    if (s_alarm_help_label != NULL)
    {
        lv_label_set_text(s_alarm_help_label, "UP/DN adj  U+D cancel\nCENTER next  LONG save");
    }

    lv_obj_clear_flag(s_alarm_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_alarm_overlay);
}

/* =========================
 * Menu overlay
 * ========================= */
void ui_create_menu_overlay(lv_obj_t *scr)
{
    s_menu_overlay = lv_obj_create(scr);
    lv_obj_set_size(s_menu_overlay, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_center(s_menu_overlay);
    lv_obj_set_style_bg_color(s_menu_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_menu_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_menu_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_menu_overlay, 0, 0);
    lv_obj_clear_flag(s_menu_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_menu_overlay, LV_OBJ_FLAG_HIDDEN);

    s_menu_title_label = lv_label_create(s_menu_overlay);
    lv_obj_set_width(s_menu_title_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_menu_title_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_menu_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_menu_title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_menu_title_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_menu_title_label, "MENU");
    lv_obj_set_pos(s_menu_title_label, 0, 6);

    for (int i = 0; i < 4; i++)
    {
        s_menu_row_bg[i] = lv_obj_create(s_menu_overlay);
        lv_obj_set_size(s_menu_row_bg[i], DISPLAY_WIDTH - 8, 20);
        lv_obj_set_pos(s_menu_row_bg[i], 4, 27 + i * 18);
        lv_obj_set_style_bg_color(s_menu_row_bg[i], lv_color_hex(0x00FFCC), 0);
        lv_obj_set_style_bg_opa(s_menu_row_bg[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_menu_row_bg[i], 0, 0);
        lv_obj_set_style_radius(s_menu_row_bg[i], 4, 0);
        lv_obj_set_style_pad_all(s_menu_row_bg[i], 0, 0);
        lv_obj_clear_flag(s_menu_row_bg[i], LV_OBJ_FLAG_SCROLLABLE);

        s_menu_item_labels[i] = lv_label_create(s_menu_overlay);
        lv_obj_set_width(s_menu_item_labels[i], DISPLAY_WIDTH - 24);
        lv_label_set_long_mode(s_menu_item_labels[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(s_menu_item_labels[i], LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_color(s_menu_item_labels[i], lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(s_menu_item_labels[i], &lv_font_montserrat_12, 0);
        lv_label_set_text(s_menu_item_labels[i], "");
        lv_obj_set_pos(s_menu_item_labels[i], 12, 31 + i * 18);
    }

    s_menu_help_label = lv_label_create(s_menu_overlay);
    lv_obj_set_width(s_menu_help_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_menu_help_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_menu_help_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_menu_help_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(s_menu_help_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(s_menu_help_label, "UP/DN move\nCENTER enter/exit");
    lv_obj_set_pos(s_menu_help_label, 0, 104);
}

void ui_update_menu_overlay_locked(bool menu_open,
                                   bool app_clock_mode,
                                   int selected,
                                   int top_index,
                                   int item_count,
                                   ui_menu_item_text_cb_t item_text_cb)
{
    if (s_menu_overlay == NULL)
    {
        return;
    }

    if (!menu_open || !app_clock_mode)
    {
        lv_obj_add_flag(s_menu_overlay, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (s_menu_title_label != NULL)
    {
        lv_label_set_text(s_menu_title_label, "MENU");
    }

    for (int i = 0; i < 4; i++)
    {
        int item_index = top_index + i;

        if (s_menu_item_labels[i] == NULL)
        {
            continue;
        }

        if (item_index >= item_count)
        {
            if (s_menu_row_bg[i] != NULL)
            {
                lv_obj_add_flag(s_menu_row_bg[i], LV_OBJ_FLAG_HIDDEN);
            }
            lv_label_set_text(s_menu_item_labels[i], "");
            continue;
        }

        bool is_selected = (item_index == selected);
        const char *text = (item_text_cb != NULL) ? item_text_cb(item_index) : "";

        if (s_menu_row_bg[i] != NULL)
        {
            lv_obj_clear_flag(s_menu_row_bg[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_opa(s_menu_row_bg[i],
                                    is_selected ? LV_OPA_COVER : LV_OPA_TRANSP,
                                    0);
        }

        lv_label_set_text(s_menu_item_labels[i], text);

        lv_obj_set_style_text_color(s_menu_item_labels[i],
                                    is_selected ? lv_color_hex(0x101010) : lv_color_hex(0xFFFFFF),
                                    0);

        lv_obj_set_style_text_font(s_menu_item_labels[i],
                                   &lv_font_montserrat_12,
                                   0);
    }

    if (s_menu_help_label != NULL)
    {
        lv_label_set_text(s_menu_help_label, "UP/DN move\nCENTER enter/exit");
    }

    lv_obj_clear_flag(s_menu_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_menu_overlay);
}

/* =========================
 * Confirm overlay
 * ========================= */
void ui_create_confirm_overlay(lv_obj_t *scr)
{
    s_confirm_overlay = lv_obj_create(scr);
    lv_obj_set_size(s_confirm_overlay, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_center(s_confirm_overlay);
    lv_obj_set_style_bg_color(s_confirm_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_confirm_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_confirm_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_confirm_overlay, 0, 0);
    lv_obj_clear_flag(s_confirm_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_confirm_overlay, LV_OBJ_FLAG_HIDDEN);

    s_confirm_title_label = lv_label_create(s_confirm_overlay);
    lv_obj_set_width(s_confirm_title_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_confirm_title_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_confirm_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_confirm_title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_confirm_title_label, &lv_font_montserrat_16, 0);
    lv_label_set_text(s_confirm_title_label, "CONFIRM");
    lv_obj_set_pos(s_confirm_title_label, 0, 8);

    s_confirm_msg_label = lv_label_create(s_confirm_overlay);
    lv_obj_set_width(s_confirm_msg_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_confirm_msg_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_confirm_msg_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_confirm_msg_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(s_confirm_msg_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_confirm_msg_label, "Are you sure?");
    lv_obj_set_pos(s_confirm_msg_label, 0, 34);

    s_confirm_no_label = lv_label_create(s_confirm_overlay);
    lv_obj_set_width(s_confirm_no_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_confirm_no_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_confirm_no_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_confirm_no_label, lv_color_hex(0xFF4040), 0);
    lv_obj_set_style_text_font(s_confirm_no_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_confirm_no_label, "> NO");
    lv_obj_set_pos(s_confirm_no_label, 0, 68);

    s_confirm_yes_label = lv_label_create(s_confirm_overlay);
    lv_obj_set_width(s_confirm_yes_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_confirm_yes_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_confirm_yes_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_confirm_yes_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_confirm_yes_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_confirm_yes_label, "  YES");
    lv_obj_set_pos(s_confirm_yes_label, 0, 88);

    s_confirm_help_label = lv_label_create(s_confirm_overlay);
    lv_obj_set_width(s_confirm_help_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_confirm_help_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_confirm_help_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_confirm_help_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(s_confirm_help_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(s_confirm_help_label, "UP/DN select\nCENTER confirm");
    lv_obj_set_pos(s_confirm_help_label, 0, 108);
}

void ui_update_confirm_overlay_locked(bool confirm_open,
                                      bool app_clock_mode,
                                      int confirm_action,
                                      bool yes_selected)
{
    if (s_confirm_overlay == NULL)
    {
        return;
    }

    if (!confirm_open || !app_clock_mode)
    {
        lv_obj_add_flag(s_confirm_overlay, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    switch (confirm_action)
    {
    case 1: /* CONFIRM_CLEAR_WIFI */
        if (s_confirm_title_label != NULL)
        {
            lv_label_set_text(s_confirm_title_label, "CLEAR WIFI");
        }
        if (s_confirm_msg_label != NULL)
        {
            lv_label_set_text(s_confirm_msg_label, "Erase saved Wi-Fi?");
        }
        break;

    default:
        if (s_confirm_title_label != NULL)
        {
            lv_label_set_text(s_confirm_title_label, "CONFIRM");
        }
        if (s_confirm_msg_label != NULL)
        {
            lv_label_set_text(s_confirm_msg_label, "Are you sure?");
        }
        break;
    }

    if (s_confirm_no_label != NULL)
    {
        lv_label_set_text(s_confirm_no_label, yes_selected ? "  NO" : "> NO");
        lv_obj_set_style_text_color(s_confirm_no_label,
                                    yes_selected ? lv_color_hex(0xFFFFFF) : lv_color_hex(0xFF4040),
                                    0);
        lv_obj_set_style_text_font(s_confirm_no_label,
                                   yes_selected ? &lv_font_montserrat_12 : &lv_font_montserrat_14,
                                   0);
    }

    if (s_confirm_yes_label != NULL)
    {
        lv_label_set_text(s_confirm_yes_label, yes_selected ? "> YES" : "  YES");
        lv_obj_set_style_text_color(s_confirm_yes_label,
                                    yes_selected ? lv_color_hex(0xFF4040) : lv_color_hex(0xFFFFFF),
                                    0);
        lv_obj_set_style_text_font(s_confirm_yes_label,
                                   yes_selected ? &lv_font_montserrat_14 : &lv_font_montserrat_12,
                                   0);
    }

    if (s_confirm_help_label != NULL)
    {
        lv_label_set_text(s_confirm_help_label, "UP/DN select\nCENTER confirm");
    }

    lv_obj_clear_flag(s_confirm_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_confirm_overlay);
}

/* =========================
 * Portal
 * ========================= */
void ui_create_portal_ui(lv_obj_t *scr)
{
    s_portal_container = lv_obj_create(scr);
    lv_obj_set_size(s_portal_container, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_center(s_portal_container);
    lv_obj_set_style_bg_opa(s_portal_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_portal_container, 0, 0);
    lv_obj_set_style_pad_all(s_portal_container, 0, 0);
    lv_obj_clear_flag(s_portal_container, LV_OBJ_FLAG_SCROLLABLE);

    s_portal_title_label = lv_label_create(s_portal_container);
    lv_obj_set_width(s_portal_title_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_portal_title_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_portal_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_portal_title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_portal_title_label, &lv_font_montserrat_18, 0);
    lv_label_set_text(s_portal_title_label, "WIFI SETUP");
    lv_obj_set_pos(s_portal_title_label, 0, 6);

    s_portal_line1_label = lv_label_create(s_portal_container);
    lv_obj_set_width(s_portal_line1_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_portal_line1_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_portal_line1_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_portal_line1_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(s_portal_line1_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_portal_line1_label, "AP: ClockSetup");
    lv_obj_set_pos(s_portal_line1_label, 0, 30);

    s_portal_line2_label = lv_label_create(s_portal_container);
    lv_obj_set_width(s_portal_line2_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_portal_line2_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_portal_line2_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_portal_line2_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(s_portal_line2_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_portal_line2_label, "IP: 192.168.4.1");
    lv_obj_set_pos(s_portal_line2_label, 0, 46);

    s_portal_line3_label = lv_label_create(s_portal_container);
    lv_obj_set_width(s_portal_line3_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_portal_line3_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_portal_line3_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_portal_line3_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_portal_line3_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_portal_line3_label, "Connect by phone");
    lv_obj_set_pos(s_portal_line3_label, 0, 70);

    s_portal_line4_label = lv_label_create(s_portal_container);
    lv_obj_set_width(s_portal_line4_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_portal_line4_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_portal_line4_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_portal_line4_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(s_portal_line4_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_portal_line4_label, "Open http://192.168.4.1");
    lv_obj_set_pos(s_portal_line4_label, 0, 86);

    s_portal_footer_label = lv_label_create(s_portal_container);
    lv_obj_set_width(s_portal_footer_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_portal_footer_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_portal_footer_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_portal_footer_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(s_portal_footer_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(s_portal_footer_label, "Waiting for setup");
    lv_obj_set_pos(s_portal_footer_label, 0, 110);
}

void ui_set_portal_visible(bool visible)
{
    if (s_portal_container == NULL)
    {
        return;
    }

    if (visible)
    {
        lv_obj_clear_flag(s_portal_container, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(s_portal_container, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_update_portal_ui(void)
{
    if (s_portal_title_label == NULL ||
        s_portal_line1_label == NULL ||
        s_portal_line2_label == NULL ||
        s_portal_line3_label == NULL ||
        s_portal_line4_label == NULL ||
        s_portal_footer_label == NULL)
    {
        return;
    }

    lv_label_set_text(s_portal_title_label, "WIFI SETUP");

    if (!wifi_portal_is_running())
    {
        lv_obj_set_style_text_color(s_portal_line1_label, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_text_color(s_portal_line2_label, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_text_color(s_portal_line3_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_color(s_portal_line4_label, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_text_color(s_portal_footer_label, lv_color_hex(0xAAAAAA), 0);

        lv_obj_set_style_text_font(s_portal_line1_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_font(s_portal_line2_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_font(s_portal_line3_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_font(s_portal_line4_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_font(s_portal_footer_label, &lv_font_montserrat_10, 0);

        lv_label_set_text(s_portal_line1_label, "");
        lv_label_set_text(s_portal_line2_label, "");
        lv_label_set_text(s_portal_line3_label, "Starting portal...");
        lv_label_set_text(s_portal_line4_label, "");
        lv_label_set_text(s_portal_footer_label, "Please wait...");
        return;
    }

    lv_obj_set_style_text_color(s_portal_line1_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_color(s_portal_line2_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(s_portal_line1_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_font(s_portal_line2_label, &lv_font_montserrat_12, 0);

    lv_label_set_text_fmt(s_portal_line1_label, "AP: %s", WIFI_PORTAL_DEFAULT_AP_SSID);
    lv_label_set_text_fmt(s_portal_line2_label, "IP: %s", wifi_portal_get_ap_ip());

    if (wifi_portal_has_new_credentials())
    {
        lv_obj_set_style_text_color(s_portal_line3_label, lv_color_hex(0x00FFCC), 0);
        lv_obj_set_style_text_color(s_portal_line4_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_color(s_portal_footer_label, lv_color_hex(0xAAAAAA), 0);

        lv_obj_set_style_text_font(s_portal_line3_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_font(s_portal_line4_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_font(s_portal_footer_label, &lv_font_montserrat_10, 0);

        lv_label_set_text_fmt(s_portal_line3_label, "Saved: %s", wifi_portal_get_last_ssid());
        lv_label_set_text(s_portal_line4_label, "Reconnecting...");
        lv_label_set_text(s_portal_footer_label, "Please wait...");
    }
    else
    {
        lv_obj_set_style_text_color(s_portal_line3_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_color(s_portal_line4_label, lv_color_hex(0xFF0000), 0);
        lv_obj_set_style_text_color(s_portal_footer_label, lv_color_hex(0xAAAAAA), 0);

        lv_obj_set_style_text_font(s_portal_line3_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_font(s_portal_line4_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_font(s_portal_footer_label, &lv_font_montserrat_10, 0);

        lv_label_set_text(s_portal_line3_label, "Connect by phone");
        lv_label_set_text_fmt(s_portal_line4_label, "Open http://%s", wifi_portal_get_ap_ip());
        lv_label_set_text(s_portal_footer_label, "Waiting for setup");
    }
}