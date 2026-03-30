#ifndef RTC_H
#define RTC_H

#include <time.h>

/**
 * @brief 初始化 RTC 模組
 */
void my_rtc_init(void);

/**
 * @brief 從 NTP 伺服器同步時間
 */
void rtc_sync_from_ntp(void);

/**
 * @brief 從 NVS 加載上次保存的時間
 */
void rtc_load_from_nvs(void);

/**
 * @brief 將當前時間保存到 NVS
 */
void rtc_save_to_nvs(void);

/**
 * @brief 獲取當前時間戳
 * @return 當前時間戳（秒）
 */
time_t rtc_get_time(void);

#endif