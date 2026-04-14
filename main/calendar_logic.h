#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    bool calendar_is_leap_year(int year);
    int calendar_days_in_month(int year, int month);
    int calendar_first_wday(int year, int month);

    /* 依目前系統時間設定 year/month；失敗時 fallback 為 2025/1 */
    void calendar_reset_to_current_month(int *year, int *month);

    /* 若 year/month 無效則重設到當月 */
    void calendar_ensure_initialized(int *year, int *month);

    /* delta 可為正負；會保留 month 不變 */
    void calendar_change_year(int *year, int *month, int delta);

    /* delta 可為正負；會自動跨年 */
    void calendar_change_month(int *year, int *month, int delta);

#ifdef __cplusplus
}
#endif