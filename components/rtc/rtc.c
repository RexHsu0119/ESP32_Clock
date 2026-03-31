#include "my_rtc.h"

#include "esp_log.h"
#include "esp_sntp.h"
#include "nvs.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "RTC";

/* NTP 同步完成旗標 */
static volatile bool s_ntp_synced = false;

/* 時間同步回調函數 */
static void time_sync_notification_cb(struct timeval *tv)
{
    (void)tv;
    s_ntp_synced = true;
    ESP_LOGI(TAG, "NTP 回調：時間同步完成");
}

void my_rtc_init(void)
{
    ESP_LOGI(TAG, "RTC 初始化");

    /* 設置時區為台灣時間 UTC+8 */
    setenv("TZ", "CST-8", 1);
    tzset();

    ESP_LOGI(TAG, "時區已設置為 UTC+8 (台灣時間)");
}

void rtc_sync_from_ntp(void)
{
    ESP_LOGI(TAG, "開始從 NTP 伺服器同步時間...");

    s_ntp_synced = false;

    /* 若之前已啟動過 SNTP，先停止避免狀態殘留 */
    if (esp_sntp_enabled())
    {
        esp_sntp_stop();
    }

    /* 已拿到 IP 後只需短暫等待 */
    ESP_LOGI(TAG, "等待網路穩定...");
    vTaskDelay(pdMS_TO_TICKS(300));

    /* 設定 SNTP */
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    sntp_set_sync_interval(3600000); /* 1 小時同步一次 */

    /* 使用較穩定的 NTP server */
    esp_sntp_setservername(0, "time.google.com");
    esp_sntp_setservername(1, "time.cloudflare.com");
    esp_sntp_setservername(2, "pool.ntp.org");

    sntp_set_time_sync_notification_cb(time_sync_notification_cb);

    /* 啟動 SNTP */
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP 客戶端已啟動，等待時間同步...");

    /* 最多等待 10 秒 */
    const int retry_count = 10;
    for (int retry = 0; retry < retry_count; retry++)
    {
        if (s_ntp_synced)
        {
            time_t now = time(NULL);
            struct tm timeinfo;

            if (localtime_r(&now, &timeinfo) != NULL)
            {
                ESP_LOGI(TAG, "NTP 同步成功: %04d-%02d-%02d %02d:%02d:%02d",
                         timeinfo.tm_year + 1900,
                         timeinfo.tm_mon + 1,
                         timeinfo.tm_mday,
                         timeinfo.tm_hour,
                         timeinfo.tm_min,
                         timeinfo.tm_sec);
            }

            rtc_save_to_nvs();
            return;
        }

        ESP_LOGI(TAG, "等待 NTP 同步... (%d/%d)", retry + 1, retry_count);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGW(TAG, "NTP 時間同步超時或失敗，保留目前/NVS時間");
}

void rtc_load_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("rtc", NVS_READONLY, &nvs_handle);

    if (err == ESP_OK)
    {
        int64_t saved_time_i64 = 0;
        err = nvs_get_i64(nvs_handle, "time", &saved_time_i64);

        if (err == ESP_OK)
        {
            /* 合理範圍：2020-01-01 ~ 2100-01-01 */
            if (saved_time_i64 > 1577836800LL && saved_time_i64 < 4102444800LL)
            {
                time_t saved_time = (time_t)saved_time_i64;
                struct timeval tv = {
                    .tv_sec = saved_time,
                    .tv_usec = 0};

                settimeofday(&tv, NULL);

                struct tm timeinfo;
                if (localtime_r(&saved_time, &timeinfo) != NULL)
                {
                    ESP_LOGI(TAG, "從 NVS 載入時間成功: %04d-%02d-%02d %02d:%02d:%02d",
                             timeinfo.tm_year + 1900,
                             timeinfo.tm_mon + 1,
                             timeinfo.tm_mday,
                             timeinfo.tm_hour,
                             timeinfo.tm_min,
                             timeinfo.tm_sec);
                }
            }
            else
            {
                ESP_LOGW(TAG, "NVS 中的時間值不合理，忽略載入: %" PRId64, saved_time_i64);
            }
        }
        else if (err == ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGW(TAG, "NVS 中找不到保存的時間");
        }
        else
        {
            ESP_LOGE(TAG, "讀取 NVS 時間失敗: %s", esp_err_to_name(err));
        }

        nvs_close(nvs_handle);
    }
    else if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "NVS 命名空間 'rtc' 不存在");
    }
    else
    {
        ESP_LOGE(TAG, "打開 NVS 失敗: %s", esp_err_to_name(err));
    }
}

void rtc_save_to_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("rtc", NVS_READWRITE, &nvs_handle);

    if (err == ESP_OK)
    {
        time_t now = time(NULL);
        int64_t time_i64 = (int64_t)now;

        err = nvs_set_i64(nvs_handle, "time", time_i64);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "設置 NVS 時間失敗: %s", esp_err_to_name(err));
            nvs_close(nvs_handle);
            return;
        }

        err = nvs_commit(nvs_handle);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "提交 NVS 失敗: %s", esp_err_to_name(err));
        }
        else
        {
            struct tm timeinfo;
            if (localtime_r(&now, &timeinfo) != NULL)
            {
                ESP_LOGD(TAG, "時間已保存到 NVS: %04d-%02d-%02d %02d:%02d:%02d",
                         timeinfo.tm_year + 1900,
                         timeinfo.tm_mon + 1,
                         timeinfo.tm_mday,
                         timeinfo.tm_hour,
                         timeinfo.tm_min,
                         timeinfo.tm_sec);
            }
        }

        nvs_close(nvs_handle);
    }
    else
    {
        ESP_LOGE(TAG, "打開 NVS 失敗: %s", esp_err_to_name(err));
    }
}

time_t rtc_get_time(void)
{
    return time(NULL);
}
