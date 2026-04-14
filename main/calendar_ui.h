#pragma once

#include <stdbool.h>

#include "clock_types.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C"
{
#endif

    void calendar_ui_create(lv_obj_t *scr);
    void calendar_ui_set_visible(bool visible);
    void calendar_ui_update(bool valid_time,
                            calendar_adjust_field_t adjust_field,
                            int *year,
                            int *month);

#ifdef __cplusplus
}
#endif