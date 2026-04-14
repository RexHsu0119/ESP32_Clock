#include "ui_clock.h"

#include <math.h>
#include <stdio.h>

#include "display.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define ANALOG_FACE_SIZE 76
#define ANALOG_CENTER_X (ANALOG_FACE_SIZE / 2)
#define ANALOG_CENTER_Y (ANALOG_FACE_SIZE / 2)
#define HOUR_HAND_LEN 19
#define MINUTE_HAND_LEN 28
#define SECOND_HAND_LEN 32

#define DIGIT_FIELD_WIDTH 40
#define COLON_FIELD_WIDTH 10

#define ANALOG_FACE_X ((DISPLAY_WIDTH - ANALOG_FACE_SIZE) / 2)
#define ANALOG_FACE_Y 22

static lv_obj_t *s_digital_container = NULL;
static lv_obj_t *s_analog_container = NULL;

/* digital */
static lv_obj_t *s_net_status_label = NULL;
static lv_obj_t *s_date_label = NULL;
static lv_obj_t *s_weekday_label = NULL;
static lv_obj_t *s_hour_label = NULL;
static lv_obj_t *s_minute_label = NULL;
static lv_obj_t *s_second_label = NULL;
static lv_obj_t *s_colon1_label = NULL;
static lv_obj_t *s_colon2_label = NULL;
static lv_obj_t *s_weather_label = NULL;

/* analog */
static lv_obj_t *s_analog_status_label = NULL;
static lv_obj_t *s_analog_date_label = NULL;
static lv_obj_t *s_analog_weekday_label = NULL;
static lv_obj_t *s_analog_weather_label = NULL;
static lv_obj_t *s_analog_face = NULL;
static lv_obj_t *s_hour_hand = NULL;
static lv_obj_t *s_minute_hand = NULL;
static lv_obj_t *s_second_hand = NULL;
static lv_obj_t *s_center_dot = NULL;
static lv_obj_t *s_tick_marks[12] = {0};

static lv_point_precise_t s_hour_points[2];
static lv_point_precise_t s_minute_points[2];
static lv_point_precise_t s_second_points[2];
static lv_point_precise_t s_tick_points[12][2];

static void set_field_text(lv_obj_t *label, int value, bool visible)
{
    if (label == NULL)
    {
        return;
    }

    if (visible)
    {
        lv_label_set_text_fmt(label, "%02d", value);
    }
    else
    {
        lv_label_set_text(label, "  ");
    }
}

static void update_hand_points(lv_obj_t *line,
                               lv_point_precise_t points[2],
                               int angle_deg,
                               int length)
{
    if (line == NULL)
    {
        return;
    }

    float rad = (float)(angle_deg - 90) * (float)M_PI / 180.0f;

    int end_x = ANALOG_CENTER_X + (int)(cosf(rad) * length);
    int end_y = ANALOG_CENTER_Y + (int)(sinf(rad) * length);

    points[0].x = ANALOG_CENTER_X;
    points[0].y = ANALOG_CENTER_Y;
    points[1].x = end_x;
    points[1].y = end_y;

    lv_line_set_points(line, points, 2);
}

static void create_analog_ticks(void)
{
    if (s_analog_face == NULL)
    {
        return;
    }

    const int outer_r = (ANALOG_FACE_SIZE / 2) - 3;

    for (int i = 0; i < 12; i++)
    {
        int inner_r;
        int line_width;

        if (i % 3 == 0)
        {
            inner_r = outer_r - 8;
            line_width = 3;
        }
        else
        {
            inner_r = outer_r - 5;
            line_width = 2;
        }

        float rad = (float)(i * 30 - 90) * (float)M_PI / 180.0f;

        int x1 = ANALOG_CENTER_X + (int)(cosf(rad) * inner_r);
        int y1 = ANALOG_CENTER_Y + (int)(sinf(rad) * inner_r);
        int x2 = ANALOG_CENTER_X + (int)(cosf(rad) * outer_r);
        int y2 = ANALOG_CENTER_Y + (int)(sinf(rad) * outer_r);

        s_tick_points[i][0].x = x1;
        s_tick_points[i][0].y = y1;
        s_tick_points[i][1].x = x2;
        s_tick_points[i][1].y = y2;

        s_tick_marks[i] = lv_line_create(s_analog_face);
        lv_obj_set_style_line_width(s_tick_marks[i], line_width, 0);
        lv_obj_set_style_line_color(s_tick_marks[i], lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_line_rounded(s_tick_marks[i], true, 0);
        lv_line_set_points(s_tick_marks[i], s_tick_points[i], 2);
    }
}

void ui_clock_create_digital(lv_obj_t *scr)
{
    s_digital_container = lv_obj_create(scr);
    lv_obj_set_size(s_digital_container, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_center(s_digital_container);
    lv_obj_set_style_bg_opa(s_digital_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_digital_container, 0, 0);
    lv_obj_set_style_pad_all(s_digital_container, 0, 0);
    lv_obj_clear_flag(s_digital_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *top_row = lv_obj_create(s_digital_container);
    lv_obj_set_size(top_row, DISPLAY_WIDTH - 4, 12);
    lv_obj_align(top_row, LV_ALIGN_TOP_MID, 0, 1);
    lv_obj_set_style_bg_opa(top_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_row, 0, 0);
    lv_obj_set_style_pad_all(top_row, 0, 0);
    lv_obj_set_style_pad_column(top_row, 0, 0);
    lv_obj_clear_flag(top_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(top_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_row,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    s_net_status_label = lv_label_create(top_row);
    lv_obj_set_width(s_net_status_label, 52);
    lv_label_set_long_mode(s_net_status_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_net_status_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(s_net_status_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(s_net_status_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(s_net_status_label, "SYNCING");

    s_date_label = lv_label_create(top_row);
    lv_obj_set_width(s_date_label, 76);
    lv_label_set_long_mode(s_date_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_date_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_date_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(s_date_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_date_label, "2026/03/29");

    s_weekday_label = lv_label_create(top_row);
    lv_obj_set_width(s_weekday_label, 28);
    lv_label_set_long_mode(s_weekday_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_weekday_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(s_weekday_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(s_weekday_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(s_weekday_label, "SUN");

    lv_obj_t *time_row = lv_obj_create(s_digital_container);
    lv_obj_set_size(time_row,
                    DIGIT_FIELD_WIDTH * 3 + COLON_FIELD_WIDTH * 2,
                    34);
    lv_obj_align(time_row, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(time_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(time_row, 0, 0);
    lv_obj_set_style_pad_all(time_row, 0, 0);
    lv_obj_set_style_pad_column(time_row, 0, 0);
    lv_obj_clear_flag(time_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(time_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_row,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    s_hour_label = lv_label_create(time_row);
    lv_obj_set_width(s_hour_label, DIGIT_FIELD_WIDTH);
    lv_obj_set_style_text_align(s_hour_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_hour_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(s_hour_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_hour_label, "00");

    s_colon1_label = lv_label_create(time_row);
    lv_obj_set_width(s_colon1_label, COLON_FIELD_WIDTH);
    lv_obj_set_style_text_align(s_colon1_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_colon1_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(s_colon1_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_colon1_label, ":");

    s_minute_label = lv_label_create(time_row);
    lv_obj_set_width(s_minute_label, DIGIT_FIELD_WIDTH);
    lv_obj_set_style_text_align(s_minute_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_minute_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(s_minute_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_minute_label, "00");

    s_colon2_label = lv_label_create(time_row);
    lv_obj_set_width(s_colon2_label, COLON_FIELD_WIDTH);
    lv_obj_set_style_text_align(s_colon2_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_colon2_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(s_colon2_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_colon2_label, ":");

    s_second_label = lv_label_create(time_row);
    lv_obj_set_width(s_second_label, DIGIT_FIELD_WIDTH);
    lv_obj_set_style_text_align(s_second_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_second_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(s_second_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_second_label, "00");

    s_weather_label = lv_label_create(s_digital_container);
    lv_obj_set_width(s_weather_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_weather_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_weather_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_weather_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(s_weather_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_weather_label, "__C  __%RH");
    lv_obj_align(s_weather_label, LV_ALIGN_BOTTOM_MID, 0, -2);
}

void ui_clock_create_analog(lv_obj_t *scr)
{
    s_analog_container = lv_obj_create(scr);
    lv_obj_set_size(s_analog_container, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_center(s_analog_container);
    lv_obj_set_style_bg_opa(s_analog_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_analog_container, 0, 0);
    lv_obj_set_style_pad_all(s_analog_container, 0, 0);
    lv_obj_clear_flag(s_analog_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *top_row = lv_obj_create(s_analog_container);
    lv_obj_set_size(top_row, DISPLAY_WIDTH - 4, 12);
    lv_obj_align(top_row, LV_ALIGN_TOP_MID, 0, 1);
    lv_obj_set_style_bg_opa(top_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_row, 0, 0);
    lv_obj_set_style_pad_all(top_row, 0, 0);
    lv_obj_set_style_pad_column(top_row, 0, 0);
    lv_obj_clear_flag(top_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(top_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_row,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    s_analog_status_label = lv_label_create(top_row);
    lv_obj_set_width(s_analog_status_label, 52);
    lv_label_set_long_mode(s_analog_status_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_analog_status_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(s_analog_status_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(s_analog_status_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(s_analog_status_label, "SYNCING");

    s_analog_date_label = lv_label_create(top_row);
    lv_obj_set_width(s_analog_date_label, 76);
    lv_label_set_long_mode(s_analog_date_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_analog_date_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_analog_date_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(s_analog_date_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_analog_date_label, "2026/03/29");

    s_analog_weekday_label = lv_label_create(top_row);
    lv_obj_set_width(s_analog_weekday_label, 28);
    lv_label_set_long_mode(s_analog_weekday_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_analog_weekday_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(s_analog_weekday_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(s_analog_weekday_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(s_analog_weekday_label, "SUN");

    s_analog_face = lv_obj_create(s_analog_container);
    lv_obj_set_size(s_analog_face, ANALOG_FACE_SIZE, ANALOG_FACE_SIZE);
    lv_obj_set_pos(s_analog_face, ANALOG_FACE_X, ANALOG_FACE_Y);
    lv_obj_set_style_radius(s_analog_face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_analog_face, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_analog_face, 2, 0);
    lv_obj_set_style_border_color(s_analog_face, lv_color_white(), 0);
    lv_obj_set_style_pad_all(s_analog_face, 0, 0);
    lv_obj_clear_flag(s_analog_face, LV_OBJ_FLAG_SCROLLABLE);

    create_analog_ticks();

    s_hour_hand = lv_line_create(s_analog_face);
    lv_obj_set_style_line_width(s_hour_hand, 4, 0);
    lv_obj_set_style_line_color(s_hour_hand, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_line_rounded(s_hour_hand, true, 0);

    s_minute_hand = lv_line_create(s_analog_face);
    lv_obj_set_style_line_width(s_minute_hand, 3, 0);
    lv_obj_set_style_line_color(s_minute_hand, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_line_rounded(s_minute_hand, true, 0);

    s_second_hand = lv_line_create(s_analog_face);
    lv_obj_set_style_line_width(s_second_hand, 2, 0);
    lv_obj_set_style_line_color(s_second_hand, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_line_rounded(s_second_hand, true, 0);

    s_center_dot = lv_obj_create(s_analog_face);
    lv_obj_set_size(s_center_dot, 7, 7);
    lv_obj_set_style_radius(s_center_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_center_dot, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_center_dot, 0, 0);
    lv_obj_center(s_center_dot);

    s_analog_weather_label = lv_label_create(s_analog_container);
    lv_obj_set_width(s_analog_weather_label, DISPLAY_WIDTH);
    lv_label_set_long_mode(s_analog_weather_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_analog_weather_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_analog_weather_label, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(s_analog_weather_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_analog_weather_label, "__C  __%RH");
    lv_obj_align(s_analog_weather_label, LV_ALIGN_BOTTOM_MID, 0, -2);
}

void ui_clock_set_digital_panel_visible(bool visible)
{
    if (s_digital_container == NULL)
    {
        return;
    }

    if (visible)
        lv_obj_clear_flag(s_digital_container, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(s_digital_container, LV_OBJ_FLAG_HIDDEN);
}

void ui_clock_set_analog_panel_visible(bool visible)
{
    if (s_analog_container == NULL)
    {
        return;
    }

    if (visible)
        lv_obj_clear_flag(s_analog_container, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(s_analog_container, LV_OBJ_FLAG_HIDDEN);
}

void ui_clock_set_digital_top_info(const char *status,
                                   int year,
                                   int month,
                                   int day,
                                   const char *weekday)
{
    if (s_net_status_label != NULL)
        lv_label_set_text(s_net_status_label, status ? status : "");

    if (s_date_label != NULL)
        lv_label_set_text_fmt(s_date_label, "%04d/%02d/%02d", year, month, day);

    if (s_weekday_label != NULL)
        lv_label_set_text(s_weekday_label, weekday ? weekday : "");
}

void ui_clock_set_analog_top_info(const char *status,
                                  int year,
                                  int month,
                                  int day,
                                  const char *weekday)
{
    if (s_analog_status_label != NULL)
        lv_label_set_text(s_analog_status_label, status ? status : "");

    if (s_analog_date_label != NULL)
        lv_label_set_text_fmt(s_analog_date_label, "%04d/%02d/%02d", year, month, day);

    if (s_analog_weekday_label != NULL)
        lv_label_set_text(s_analog_weekday_label, weekday ? weekday : "");
}

void ui_clock_set_digital_top_info_unknown(const char *status)
{
    if (s_net_status_label != NULL)
        lv_label_set_text(s_net_status_label, status ? status : "");
    if (s_date_label != NULL)
        lv_label_set_text(s_date_label, "__/__/__");
    if (s_weekday_label != NULL)
        lv_label_set_text(s_weekday_label, "__");
}

void ui_clock_set_analog_top_info_unknown(const char *status)
{
    if (s_analog_status_label != NULL)
        lv_label_set_text(s_analog_status_label, status ? status : "");
    if (s_analog_date_label != NULL)
        lv_label_set_text(s_analog_date_label, "__/__/__");
    if (s_analog_weekday_label != NULL)
        lv_label_set_text(s_analog_weekday_label, "__");
}

void ui_clock_set_digital_time(int hour,
                               int minute,
                               int second,
                               bool show_hour,
                               bool show_minute,
                               bool show_second)
{
    set_field_text(s_hour_label, hour, show_hour);
    set_field_text(s_minute_label, minute, show_minute);
    set_field_text(s_second_label, second, show_second);
}

void ui_clock_set_digital_time_unknown(void)
{
    if (s_hour_label != NULL)
        lv_label_set_text(s_hour_label, "__");
    if (s_minute_label != NULL)
        lv_label_set_text(s_minute_label, "__");
    if (s_second_label != NULL)
        lv_label_set_text(s_second_label, "__");
}

void ui_clock_set_analog_time(int hour, int minute, int second)
{
    int hour_angle = (hour % 12) * 30 + (minute / 2);
    int minute_angle = minute * 6;
    int second_angle = second * 6;

    update_hand_points(s_hour_hand, s_hour_points, hour_angle, HOUR_HAND_LEN);
    update_hand_points(s_minute_hand, s_minute_points, minute_angle, MINUTE_HAND_LEN);
    update_hand_points(s_second_hand, s_second_points, second_angle, SECOND_HAND_LEN);
}

void ui_clock_set_analog_visible(bool visible)
{
    if (s_hour_hand != NULL)
    {
        if (visible)
            lv_obj_clear_flag(s_hour_hand, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_hour_hand, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_minute_hand != NULL)
    {
        if (visible)
            lv_obj_clear_flag(s_minute_hand, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_minute_hand, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_second_hand != NULL)
    {
        if (visible)
            lv_obj_clear_flag(s_second_hand, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_second_hand, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_center_dot != NULL)
    {
        if (visible)
            lv_obj_clear_flag(s_center_dot, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_center_dot, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_clock_set_weather_text(const char *text, lv_color_t color)
{
    if (s_weather_label != NULL)
    {
        lv_obj_set_style_text_color(s_weather_label, color, 0);
        lv_label_set_text(s_weather_label, text ? text : "");
    }

    if (s_analog_weather_label != NULL)
    {
        lv_obj_set_style_text_color(s_analog_weather_label, color, 0);
        lv_label_set_text(s_analog_weather_label, text ? text : "");
    }
}