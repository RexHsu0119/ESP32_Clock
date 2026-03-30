#include "my_rtc.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <sys/time.h>
#include <string.h>

static const char *TAG = "RTC";

// 時間同步回調函數
static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "時間已從 NTP 伺服器同步");
    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);
    ESP_LOGI(TAG, "當前時間: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo->tm_year + 1900,
             timeinfo->tm_mon + 1,
             timeinfo->tm_mday,
             timeinfo->tm_hour,
             timeinfo->tm_min,
             timeinfo->tm_sec);
}

void my_rtc_init(void)
{
    ESP_LOGI(TAG, "RTC 初始化");

    // 設置時區為臺灣時間 (UTC+8)
    setenv("TZ", "CST-8", 1);
    tzset();

    ESP_LOGI(TAG, "時區已設置為 UTC+8 (臺灣時間)");
}

void rtc_sync_from_ntp(void)
{
    ESP_LOGI(TAG, "開始從 NTP 伺服器同步時間...");

    // 設置 SNTP 操作模式
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);

    // 設置 NTP 伺服器
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.nist.gov");
    esp_sntp_setservername(2, "time.google.com");

    // 設置時間同步通知回調
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);

    // 啟動 SNTP
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP 客戶端已啟動，等待時間同步...");

    // 等待時間同步（最多 30 秒）
    int retry_count = 0;
    const int max_retries = 30;

    while (retry_count < max_retries)
    {
        time_t now = time(NULL);

        // 檢查時間是否已同步（時間戳應大於 2024 年 1 月 1 日）
        if (now > 1704067200)
        {
            ESP_LOGI(TAG, "NTP 同步成功");
            rtc_save_to_nvs();
            esp_sntp_stop();
            return;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
        retry_count++;
        ESP_LOGD(TAG, "等待 NTP 同步... (%d/%d)", retry_count, max_retries);
    }

    ESP_LOGW(TAG, "NTP 同步超時，停止 SNTP 客戶端");
    esp_sntp_stop();
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
            time_t saved_time = (time_t)saved_time_i64;
            struct timeval tv = {
                .tv_sec = saved_time,
                .tv_usec = 0};
            settimeofday(&tv, NULL);

            struct tm *timeinfo = localtime(&saved_time);
            ESP_LOGI(TAG, "從 NVS 加載時間成功: %04d-%02d-%02d %02d:%02d:%02d",
                     timeinfo->tm_year + 1900,
                     timeinfo->tm_mon + 1,
                     timeinfo->tm_mday,
                     timeinfo->tm_hour,
                     timeinfo->tm_min,
                     timeinfo->tm_sec);
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
        }

        err = nvs_commit(nvs_handle);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "提交 NVS 失敗: %s", esp_err_to_name(err));
        }
        else
        {
            struct tm *timeinfo = localtime(&now);
            ESP_LOGD(TAG, "時間已保存到 NVS: %04d-%02d-%02d %02d:%02d:%02d",
                     timeinfo->tm_year + 1900,
                     timeinfo->tm_mon + 1,
                     timeinfo->tm_mday,
                     timeinfo->tm_hour,
                     timeinfo->tm_min,
                     timeinfo->tm_sec);
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