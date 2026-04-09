#ifndef WIFI_H
#define WIFI_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_wifi_types.h"

#define WIFI_SCAN_MAX_APS 20
#define WIFI_SCAN_SSID_MAX_LEN 32

typedef struct
{
    char ssid[WIFI_SCAN_SSID_MAX_LEN + 1];
    int8_t rssi;
    wifi_auth_mode_t authmode;
} wifi_scan_result_t;

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
 * @brief 掃描附近的 Wi-Fi AP
 * @param results 掃描結果陣列
 * @param count 輸入為 results 可容納最大筆數，輸出為實際回傳筆數
 * @param max_count results 陣列容量
 * @return true 成功，false 失敗
 */
bool wifi_scan_networks(wifi_scan_result_t *results, uint16_t *count, uint16_t max_count);

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