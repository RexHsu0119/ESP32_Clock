#include "calendar_ui.h"

#include <time.h>

#include "display.h"
#include "calendar_logic.h"

static lv_obj_t *s_calendar_container = NULL;
static lv_obj_t *s_calendar_title_label = NULL;
static lv_obj_t *s_calendar_header_row = NULL;
static lv_obj_t *s_calendar_year_label = NULL;
static lv_obj_t *s_calendar_separator_label = NULL;
static lv_obj_t *s_calendar_month_label = NULL;
static lv_obj_t *s_calendar_weekday_labels[7] = {0};
static lv_obj_t *s_calendar_day_labels[6][7] = {0};

static void calendar_ui_apply_adjust_highlight(calendar_adjust_field_t adjust_field)
{
    bool year_active = (adjust_field == CALENDAR_ADJUST_YEAR);

    if (s_calendar_year_label == NULL || s_calendar_month_label == NULL)
    {
        return;
    }

    lv_obj_set_style_text_color(s_calendar_year_label,
                                year_active ? lv_color_hex(0x101010) : lv_color_hex(0x8FA39F),
                                0);
    lv_obj_set_style_bg_color(s_calendar_year_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_bg_opa(s_calendar_year_label,
                            year_active ? LV_OPA_COVER : LV_OPA_TRANSP,
                            0);
    lv_obj_set_style_radius(s_calendar_year_label, 4, 0);
    lv_obj_set_style_pad_left(s_calendar_year_label, 4, 0);
    lv_obj_set_style_pad_right(s_calendar_year_label, 4, 0);

    lv_obj_set_style_text_color(s_calendar_month_label,
                                year_active ? lv_color_hex(0x00FFCC) : lv_color_hex(0x101010),
                                0);
    lv_obj_set_style_bg_color(s_calendar_month_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_bg_opa(s_calendar_month_label,
                            year_active ? LV_OPA_TRANSP : LV_OPA_COVER,
                            0);
    lv_obj_set_style_radius(s_calendar_month_label, 4, 0);
    lv_obj_set_style_pad_left(s_calendar_month_label, 4, 0);
    lv_obj_set_style_pad_right(s_calendar_month_label, 4, 0);

    if (s_calendar_separator_label != NULL)
    {
        lv_obj_set_style_text_color(s_calendar_separator_label, lv_color_hex(0x6E807C), 0);
    }
}

void calendar_ui_create(lv_obj_t *scr)
{
    static const char *week_names[7] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
    const int cell_w = 22;
    const int weekday_cell_h = 11;
    const int day_cell_h = 14;
    const int start_x = 3;
    const int head_y = 43;
    const int day_start_y = 53;
    const int row_step = 12;

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

    s_calendar_header_row = lv_obj_create(s_calendar_container);
    lv_obj_set_size(s_calendar_header_row, DISPLAY_WIDTH, 20);
    lv_obj_set_pos(s_calendar_header_row, 0, 20);
    lv_obj_set_style_bg_opa(s_calendar_header_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_calendar_header_row, 0, 0);
    lv_obj_set_style_pad_all(s_calendar_header_row, 0, 0);
    lv_obj_set_style_pad_column(s_calendar_header_row, 2, 0);
    lv_obj_clear_flag(s_calendar_header_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(s_calendar_header_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_calendar_header_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_calendar_header_row,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    s_calendar_year_label = lv_label_create(s_calendar_header_row);
    lv_obj_set_style_text_font(s_calendar_year_label, &lv_font_montserrat_18, 0);
    lv_label_set_text(s_calendar_year_label, "2025");

    s_calendar_separator_label = lv_label_create(s_calendar_header_row);
    lv_obj_set_style_text_font(s_calendar_separator_label, &lv_font_montserrat_18, 0);
    lv_label_set_text(s_calendar_separator_label, "/");

    s_calendar_month_label = lv_label_create(s_calendar_header_row);
    lv_obj_set_style_text_font(s_calendar_month_label, &lv_font_montserrat_18, 0);
    lv_label_set_text(s_calendar_month_label, "01");

    calendar_ui_apply_adjust_highlight(CALENDAR_ADJUST_MONTH);

    for (int c = 0; c < 7; c++)
    {
        s_calendar_weekday_labels[c] = lv_label_create(s_calendar_container);
        lv_obj_set_size(s_calendar_weekday_labels[c], cell_w, weekday_cell_h);
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
            lv_obj_set_size(s_calendar_day_labels[r][c], cell_w, day_cell_h);
            lv_obj_set_pos(s_calendar_day_labels[r][c], start_x + c * cell_w, day_start_y + r * row_step);
            lv_obj_set_style_text_align(s_calendar_day_labels[r][c], LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_color(s_calendar_day_labels[r][c], lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(s_calendar_day_labels[r][c], &lv_font_montserrat_12, 0);
            lv_obj_set_style_bg_opa(s_calendar_day_labels[r][c], LV_OPA_TRANSP, 0);
            lv_label_set_text(s_calendar_day_labels[r][c], "");
        }
    }
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

void calendar_ui_update(bool valid_time,
                        calendar_adjust_field_t adjust_field,
                        int *year,
                        int *month)
{
    static const char *week_names[7] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};

    if (s_calendar_container == NULL ||
        s_calendar_year_label == NULL ||
        s_calendar_month_label == NULL ||
        year == NULL ||
        month == NULL)
    {
        return;
    }

    calendar_ui_apply_adjust_highlight(adjust_field);

    for (int i = 0; i < 7; i++)
    {
        if (s_calendar_weekday_labels[i] != NULL)
        {
            lv_label_set_text(s_calendar_weekday_labels[i], week_names[i]);
        }
    }

    calendar_ensure_initialized(year, month);

    lv_label_set_text_fmt(s_calendar_year_label, "%04d", *year);
    lv_label_set_text_fmt(s_calendar_month_label, "%02d", *month);

    int first_wday = calendar_first_wday(*year, *month);
    int days = calendar_days_in_month(*year, *month);

    time_t now = time(NULL);
    struct tm today;
    bool has_today = valid_time && (localtime_r(&now, &today) != NULL);

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
}