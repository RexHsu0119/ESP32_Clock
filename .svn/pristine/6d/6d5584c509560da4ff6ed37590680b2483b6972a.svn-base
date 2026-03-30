#ifndef WIFI_H
#define WIFI_H

#include <stdbool.h>

/**
 * @brief 初始化 WIFI 模組
 * @return true 成功，false 失敗
 */
bool wifi_init(void);

/**
 * @brief 連接到指定的 WIFI 網路
 * @param ssid WIFI 名稱
 * @param password WIFI 密碼
 * @return true 連接成功，false 連接失敗
 */
bool wifi_connect(const char *ssid, const char *password);

/**
 * @brief 斷開 WIFI 連接
 */
void wifi_disconnect(void);

/**
 * @brief 檢查 WIFI 連接狀態
 * @return true 已連接，false 未連接
 */
bool wifi_is_connected(void);

#endif