#pragma once

#include <stdbool.h>

#include "clock_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    const char *alarm_repeat_text(int repeat);
    bool alarm_save_all_to_nvs(const alarm_config_t *alarms, int count);
    void alarm_load_all_from_nvs(alarm_config_t *alarms, int count);

#ifdef __cplusplus
}
#endif