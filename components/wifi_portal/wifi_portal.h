#ifndef WIFI_PORTAL_H
#define WIFI_PORTAL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define WIFI_PORTAL_DEFAULT_AP_SSID "ClockSetup"
#define WIFI_PORTAL_DEFAULT_AP_PASSWORD "0123456789"
#define WIFI_PORTAL_DEFAULT_AP_IP "192.168.4.1"

    /**
     * @brief 啟動 Wi-Fi 配網入口網站（SoftAP + HTTP Server）
     * @param ap_ssid SoftAP SSID，傳 NULL 則用預設值
     * @param ap_password SoftAP 密碼，傳 NULL 則用預設值
     * @return true 成功，false 失敗
     */
    bool wifi_portal_start(const char *ap_ssid, const char *ap_password);

    /**
     * @brief 停止 Wi-Fi 配網入口網站
     */
    void wifi_portal_stop(void);

    /**
     * @brief 查詢入口網站是否正在運行
     * @return true 正在運行，false 未運行
     */
    bool wifi_portal_is_running(void);

    /**
     * @brief 查詢是否有新的 Wi-Fi credentials 已經透過網頁送出並存入 NVS
     * @return true 有，false 沒有
     */
    bool wifi_portal_has_new_credentials(void);

    /**
     * @brief 清除 new credentials 旗標
     */
    void wifi_portal_clear_new_credentials_flag(void);

    /**
     * @brief 取得入口網站 AP IP 字串
     * @return 固定字串，例如 "192.168.4.1"
     */
    const char *wifi_portal_get_ap_ip(void);

    /**
     * @brief 取得最近一次成功送出的 SSID
     * @return SSID 字串
     */
    const char *wifi_portal_get_last_ssid(void);

#ifdef __cplusplus
}
#endif

#endif