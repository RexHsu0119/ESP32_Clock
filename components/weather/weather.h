#ifndef WEATHER_H
#define WEATHER_H

#include <stdbool.h>

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
 * @brief 立即更新湖口鄉天氣資料
 * @return true 成功
 * @return false 失敗
 */
bool weather_update_now(void);

/**
 * @brief 取得最新天氣資料
 * @param out 輸出資料
 * @return true 有有效資料
 * @return false 尚無有效資料
 */
bool weather_get_info(weather_info_t *out);

#endif