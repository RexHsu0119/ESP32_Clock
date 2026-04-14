#include "calendar_ui.h"

#include <time.h>

#include "display.h"
#include "calendar_logic.h"

static lv_obj_t *s_calendar_container = NULL;
static lv_obj_t *s_calendar_title_label = NULL;
static lv_obj_t *s_calendar_month_label = NULL;
static lv_obj_t *s_calendar_weekday_labels[7] = {0};
static lv_obj_t *s_calendar_day_labels[6][7] = {0};
static lv_obj_t *s_calendar_footer_label = NULL;

void calendar_ui_create(lv_obj_t *scr)
{
    static const char *week_names[7] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
    const int cell_w = 22;
    const int cell_h = 10;
    const int start_x = 3;
    const int head_y = 42;
    const int day_start_y = 56;
    const int row_step = 9;

    s_calendar_container = lv_obj_create(scr);
    lv_obj_set_size(s_calendar_container, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_center(s_calendar_container);
    lv_obj_set_style_bg_opa(s_calendar_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_calendar_container, 0, 0);
    lv_obj_set_style_pad_all(s_calendar_container, 0, 0);
    lv_obj_clear_flag(s_calendar_container, LV_OBJ_FLAG_SCROLLABLE);

    s_calendar_title_label = lv_label_create(s_calendar_container);
    lv_obj_set_width(s_calendar_title_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_calendar_title_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_calendar_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_calendar_title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_calendar_title_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_calendar_title_label, "CALENDAR");
    lv_obj_set_pos(s_calendar_title_label, 0, 4);

    s_calendar_month_label = lv_label_create(s_calendar_container);
    lv_obj_set_width(s_calendar_month_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_calendar_month_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_calendar_month_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_calendar_month_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(s_calendar_month_label, &lv_font_montserrat_18, 0);
    lv_label_set_text(s_calendar_month_label, "2025/01");
    lv_obj_set_pos(s_calendar_month_label, 0, 18);

    for (int c = 0; c < 7; c++)
    {
        s_calendar_weekday_labels[c] = lv_label_create(s_calendar_container);
        lv_obj_set_size(s_calendar_weekday_labels[c], cell_w, cell_h);
        lv_obj_set_pos(s_calendar_weekday_labels[c], start_x + c * cell_w, head_y);
        lv_obj_set_style_text_align(s_calendar_weekday_labels[c], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(s_calendar_weekday_labels[c], lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_text_font(s_calendar_weekday_labels[c], &lv_font_montserrat_10, 0);
        lv_label_set_text(s_calendar_weekday_labels[c], week_names[c]);
    }

    for (int r = 0; r < 6; r++)
    {
        for (int c = 0; c < 7; c++)
        {
            s_calendar_day_labels[r][c] = lv_label_create(s_calendar_container);
            lv_obj_set_size(s_calendar_day_labels[r][c], cell_w, cell_h);
            lv_obj_set_pos(s_calendar_day_labels[r][c], start_x + c * cell_w, day_start_y + r * row_step);
            lv_obj_set_style_text_align(s_calendar_day_labels[r][c], LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_color(s_calendar_day_labels[r][c], lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(s_calendar_day_labels[r][c], &lv_font_montserrat_10, 0);
            lv_obj_set_style_bg_opa(s_calendar_day_labels[r][c], LV_OPA_TRANSP, 0);
            lv_label_set_text(s_calendar_day_labels[r][c], "");
        }
    }

    s_calendar_footer_label = lv_label_create(s_calendar_container);
    lv_obj_set_width(s_calendar_footer_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_calendar_footer_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_calendar_footer_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_calendar_footer_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(s_calendar_footer_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(s_calendar_footer_label, "UP/DN month\nCENTER=today");
    lv_obj_set_pos(s_calendar_footer_label, 0, 112);
}

void calendar_ui_set_visible(bool visible)
{
    if (s_calendar_container == NULL)
    {
        return;
    }

    if (visible)
    {
        lv_obj_clear_flag(s_calendar_container, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(s_calendar_container, LV_OBJ_FLAG_HIDDEN);
    }
}

void calendar_ui_update(bool valid_time, int *year, int *month)
{
    static const char *week_names[7] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};

    if (s_calendar_container == NULL || s_calendar_month_label == NULL || year == NULL || month == NULL)
    {
        return;
    }

    for (int i = 0; i < 7; i++)
    {
        if (s_calendar_weekday_labels[i] != NULL)
        {
            lv_label_set_text(s_calendar_weekday_labels[i], week_names[i]);
        }
    }

    if (!valid_time)
    {
        lv_label_set_text(s_calendar_month_label, "----/--");

        for (int r = 0; r < 6; r++)
        {
            for (int c = 0; c < 7; c++)
            {
                lv_obj_t *obj = s_calendar_day_labels[r][c];
                if (obj != NULL)
                {
                    lv_label_set_text(obj, "");
                    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0x555555), 0);
                }
            }
        }

        if (s_calendar_footer_label != NULL)
        {
            lv_label_set_text(s_calendar_footer_label, "No date");
        }

        return;
    }

    calendar_ensure_initialized(year, month);

    lv_label_set_text_fmt(s_calendar_month_label, "%04d/%02d", *year, *month);

    int first_wday = calendar_first_wday(*year, *month);
    int days = calendar_days_in_month(*year, *month);

    time_t now = time(NULL);
    struct tm today;
    bool has_today = (localtime_r(&now, &today) != NULL);

    int day = 1;
    for (int r = 0; r < 6; r++)
    {
        for (int c = 0; c < 7; c++)
        {
            int idx = r * 7 + c;
            lv_obj_t *obj = s_calendar_day_labels[r][c];
            if (obj == NULL)
            {
                continue;
            }

            if (idx < first_wday || day > days)
            {
                lv_label_set_text(obj, "");
                lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
                lv_obj_set_style_text_color(obj, lv_color_hex(0x555555), 0);
            }
            else
            {
                bool is_today = has_today &&
                                (today.tm_year + 1900 == *year) &&
                                (today.tm_mon + 1 == *month) &&
                                (today.tm_mday == day);

                lv_label_set_text_fmt(obj, "%d", day);
                lv_obj_set_style_text_color(obj,
                                            is_today ? lv_color_hex(0xFF4040) : lv_color_hex(0xFFFFFF),
                                            0);

                if (is_today)
                {
                    lv_obj_set_style_bg_color(obj, lv_color_hex(0x202020), 0);
                    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
                    lv_obj_set_style_radius(obj, 4, 0);
                }
                else
                {
                    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
                }

                day++;
            }
        }
    }

    if (s_calendar_footer_label != NULL)
    {
        lv_label_set_text(s_calendar_footer_label, "UP/DN month\nCENTER=today");
    }
}