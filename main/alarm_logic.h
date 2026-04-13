#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * 目前為了相容您現有 main.c：
     * - repeat 先用 int
     * - alarm config 先用 void *
     *
     * 後續若導入 app_state.h，再改成：
     *   const char *alarm_repeat_text(alarm_repeat_t repeat);
     *   bool alarm_save_to_nvs(const alarm_config_t *alarm);
     *   void alarm_load_from_nvs(alarm_config_t *alarm);
     */

    const char *alarm_repeat_text(int repeat);
    bool alarm_save_to_nvs(const void *alarm_cfg);
    void alarm_load_from_nvs(void *alarm_cfg);

#ifdef __cplusplus
}
#endif