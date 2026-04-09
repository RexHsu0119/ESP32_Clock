#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#define WIFI_CONFIG_SSID_MAX_LEN 32
#define WIFI_CONFIG_PASSWORD_MAX_LEN 64

/**
 * @brief 儲存 Wi-Fi 帳密到 NVS
 * @param ssid Wi-Fi SSID
 * @param password Wi-Fi 密碼
 * @return true 成功，false 失敗
 */
bool wifi_config_save_credentials(const char *ssid, const char *password);

/**
 * @brief 從 NVS 載入 Wi-Fi 帳密
 * @param ssid 輸出 SSID buffer
 * @param ssid_len SSID buffer 長度
 * @param password 輸出 password buffer
 * @param password_len password buffer 長度
 * @return true 成功，false 失敗或不存在
 */
bool wifi_config_load_credentials(char *ssid,
                                  size_t ssid_len,
                                  char *password,
                                  size_t password_len);

/**
 * @brief 檢查是否已有儲存 Wi-Fi 帳密
 * @return true 有，false 沒有
 */
bool wifi_config_has_credentials(void);

/**
 * @brief 清除已儲存的 Wi-Fi 帳密
 * @return true 成功，false 失敗
 */
bool wifi_config_clear_credentials(void);

#endif