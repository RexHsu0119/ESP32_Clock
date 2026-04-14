#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C"
{
#endif

    void ui_clock_create_digital(lv_obj_t *scr);
    void ui_clock_create_analog(lv_obj_t *scr);

    void ui_clock_set_digital_panel_visible(bool visible);
    void ui_clock_set_analog_panel_visible(bool visible);

    void ui_clock_set_digital_top_info(const char *status,
                                       int year,
                                       int month,
                                       int day,
                                       const char *weekday);

    void ui_clock_set_analog_top_info(const char *status,
                                      int year,
                                      int month,
                                      int day,
                                      const char *weekday);

    void ui_clock_set_digital_top_info_unknown(const char *status);
    void ui_clock_set_analog_top_info_unknown(const char *status);

    void ui_clock_set_digital_time(int hour,
                                   int minute,
                                   int second,
                                   bool show_hour,
                                   bool show_minute,
                                   bool show_second);

    void ui_clock_set_digital_time_unknown(void);

    void ui_clock_set_analog_time(int hour, int minute, int second);
    void ui_clock_set_analog_visible(bool visible);

    void ui_clock_set_weather_text(const char *text, lv_color_t color);

#ifdef __cplusplus
}
#endif