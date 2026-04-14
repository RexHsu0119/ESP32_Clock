#include "ui.h"

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
static lv_obj_t *s_alarm_hour_label = NULL;
static lv_obj_t *s_alarm_colon_label = NULL;
static lv_obj_t *s_alarm_minute_label = NULL;
static lv_obj_t *s_alarm_enable_label = NULL;
static lv_obj_t *s_alarm_repeat_label = NULL;

/* menu overlay */
static lv_obj_t *s_menu_overlay = NULL;
static lv_obj_t *s_menu_title_label = NULL;
static lv_obj_t *s_menu_help_label = NULL;
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
    lv_obj_set_pos(s_alarm_title_label, 0, 6);

    s_alarm_hour_label = lv_label_create(s_alarm_overlay);
    lv_obj_set_width(s_alarm_hour_label, 36);
    lv_obj_set_style_text_align(s_alarm_hour_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_alarm_hour_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(s_alarm_hour_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_alarm_hour_label, "07");
    lv_obj_set_pos(s_alarm_hour_label, 34, 28);

    s_alarm_colon_label = lv_label_create(s_alarm_overlay);
    lv_obj_set_width(s_alarm_colon_label, 16);
    lv_obj_set_style_text_align(s_alarm_colon_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_alarm_colon_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(s_alarm_colon_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_alarm_colon_label, ":");
    lv_obj_set_pos(s_alarm_colon_label, 72, 28);

    s_alarm_minute_label = lv_label_create(s_alarm_overlay);
    lv_obj_set_width(s_alarm_minute_label, 36);
    lv_obj_set_style_text_align(s_alarm_minute_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_alarm_minute_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(s_alarm_minute_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_alarm_minute_label, "00");
    lv_obj_set_pos(s_alarm_minute_label, 90, 28);

    lv_obj_t *alarm_enable_prefix = lv_label_create(s_alarm_overlay);
    lv_obj_set_style_text_color(alarm_enable_prefix, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(alarm_enable_prefix, &lv_font_montserrat_14, 0);
    lv_label_set_text(alarm_enable_prefix, "Enable:");
    lv_obj_set_pos(alarm_enable_prefix, 28, 64);

    s_alarm_enable_label = lv_label_create(s_alarm_overlay);
    lv_obj_set_width(s_alarm_enable_label, 48);
    lv_obj_set_style_text_align(s_alarm_enable_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(s_alarm_enable_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(s_alarm_enable_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_alarm_enable_label, "ON");
    lv_obj_set_pos(s_alarm_enable_label, 92, 64);

    lv_obj_t *alarm_repeat_prefix = lv_label_create(s_alarm_overlay);
    lv_obj_set_style_text_color(alarm_repeat_prefix, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(alarm_repeat_prefix, &lv_font_montserrat_14, 0);
    lv_label_set_text(alarm_repeat_prefix, "Repeat:");
    lv_obj_set_pos(alarm_repeat_prefix, 28, 82);

    s_alarm_repeat_label = lv_label_create(s_alarm_overlay);
    lv_obj_set_width(s_alarm_repeat_label, 56);
    lv_obj_set_style_text_align(s_alarm_repeat_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(s_alarm_repeat_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(s_alarm_repeat_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_alarm_repeat_label, "DAILY");
    lv_obj_set_pos(s_alarm_repeat_label, 92, 82);

    s_alarm_help_label = lv_label_create(s_alarm_overlay);
    lv_obj_set_width(s_alarm_help_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_alarm_help_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_alarm_help_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_alarm_help_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(s_alarm_help_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(s_alarm_help_label, "UP/DOWN adj\nCENTER next/save");
    lv_obj_set_pos(s_alarm_help_label, 0, 104);
}

void ui_alarm_overlay_move_foreground(void)
{
    if (s_alarm_overlay != NULL)
    {
        lv_obj_move_foreground(s_alarm_overlay);
    }
}

void ui_update_alarm_overlay_locked(bool alarm_setting_mode,
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

    bool blink_on = ((esp_timer_get_time() / UI_SETTING_BLINK_PERIOD_US) % 2) == 0;

    bool show_enable = true;
    bool show_repeat = true;
    bool show_hour = true;
    bool show_minute = true;

    if (!blink_on)
    {
        switch (alarm_field)
        {
        case 0:
            show_enable = false;
            break;
        case 1:
            show_repeat = false;
            break;
        case 2:
            show_hour = false;
            break;
        case 3:
            show_minute = false;
            break;
        default:
            break;
        }
    }

    if (s_alarm_title_label != NULL)
    {
        lv_label_set_text(s_alarm_title_label, "ALARM SET");
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

    if (s_alarm_enable_label != NULL)
    {
        if (show_enable)
            lv_label_set_text(s_alarm_enable_label, enabled ? "ON" : "OFF");
        else
            lv_label_set_text(s_alarm_enable_label, "   ");
    }

    if (s_alarm_repeat_label != NULL)
    {
        if (show_repeat)
            lv_label_set_text(s_alarm_repeat_label, alarm_repeat_text(repeat));
        else
            lv_label_set_text(s_alarm_repeat_label, "     ");
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
        s_menu_item_labels[i] = lv_label_create(s_menu_overlay);
        lv_obj_set_width(s_menu_item_labels[i], DISPLAY_WIDTH - 20);
        lv_label_set_long_mode(s_menu_item_labels[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(s_menu_item_labels[i], LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_color(s_menu_item_labels[i], lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(s_menu_item_labels[i], &lv_font_montserrat_12, 0);
        lv_label_set_text(s_menu_item_labels[i], "");
        lv_obj_set_pos(s_menu_item_labels[i], 16, 30 + i * 16);
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
            lv_label_set_text(s_menu_item_labels[i], "");
            continue;
        }

        bool is_selected = (item_index == selected);
        const char *text = (item_text_cb != NULL) ? item_text_cb(item_index) : "";

        lv_label_set_text_fmt(s_menu_item_labels[i], "%c %s",
                              is_selected ? '>' : ' ',
                              text);

        lv_obj_set_style_text_color(s_menu_item_labels[i],
                                    is_selected ? lv_color_hex(0xFF4040) : lv_color_hex(0xFFFFFF),
                                    0);

        lv_obj_set_style_text_font(s_menu_item_labels[i],
                                   is_selected ? &lv_font_montserrat_14 : &lv_font_montserrat_12,
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