#ifndef WEATHER_H
#define WEATHER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        bool valid;
        float temperature_c;
        int humidity_percent;
    } weather_info_t;

    /**
     * @brief 初始化天氣模組
     */
    void weather_init(void);

    /**
     * @brief 立即更新天氣資料
     * @return true 成功，false 失敗
     */
    bool weather_update_now(void);

    /**
     * @brief 取得目前快取的天氣資訊
     * @param out 輸出結構
     * @return true 有有效資料，false 無有效資料
     */
    bool weather_get_info(weather_info_t *out);

#ifdef __cplusplus
}
#endif

#endif