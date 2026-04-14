#pragma once

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C"
{
#endif

    void calendar_ui_create(lv_obj_t *scr);
    void calendar_ui_set_visible(bool visible);
    void calendar_ui_update(bool valid_time, int *year, int *month);

#ifdef __cplusplus
}
#endif