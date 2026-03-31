#include "my_rtc.h"
#include "esp_sntp.h" // 改回使用 esp_sntp.h
#include "nvs_flash.h"
#include "esp_log.h"
#include <sys/time.h>
#include <string.h>

// 新增這兩行來支援 vTaskDelay
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

    // 0. 強制將系統本地時間設回 1970 年...
    struct timeval tv_reset = {.tv_sec = 0, .tv_usec = 0};
    settimeofday(&tv_reset, NULL);

    // ✨ 關鍵優化 1：拿到 IP 後不要急著發送，等待路由器完全開通對外網路
    ESP_LOGI(TAG, "等待網路路由穩定...");
    vTaskDelay(2000 / portTICK_PERIOD_MS); // 停頓 2 秒

    // 1. 初始化並設定 NTP 伺服器
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    // ✨ 補上這行：強制收到時間後「立刻精準覆蓋」，解決秒數慢的問題
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);

    sntp_set_sync_interval(3600000);

    // ✨ 終極優化：將「純 IP」放到第 0 順位，徹底避開手機熱點緩慢的 DNS 解析
    esp_sntp_setservername(0, "162.159.200.1");       // Cloudflare 時間伺服器 (純IP)
    esp_sntp_setservername(1, "time.stdtime.gov.tw"); // 國家時間實驗室 (備用)
    esp_sntp_setservername(2, "pool.ntp.org");        // NTP 池 (備用)

    // 3. 註冊非同步回調函數
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);

    // 4. 初始化
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP 客戶端已啟動，等待時間同步...");

    // 5. 改用「直接檢查年份」來判斷對時是否成功
    int retry = 0;
    const int retry_count = 90; // ✨ 若使用熱點，把耐心放大到 90 秒，避免早退
    bool sync_success = false;

    while (retry < retry_count)
    {
        // 抓取當前系統時間
        time_t now = time(NULL);
        struct tm timeinfo = {0};
        localtime_r(&now, &timeinfo);

        // 如果年份大於 2026 (表示真正從小於等於2026的舊時間更新到了2027年以上的未來，或至少能確保目前不是舊時間)
        // 更精確的作法：只要 NTP 對時成功，time_sync_notification_cb 就會被觸發
        // 但為了防呆，我們檢查年份：1900 + timeinfo.tm_year
        int current_year = 1900 + timeinfo.tm_year;

        ESP_LOGI(TAG, "等待 NTP 同步... 當前系統年份: %d (%d/%d)", current_year, retry + 1, retry_count);

        // ESP32 沒有電池時預設是 1970。您之前 NVS 存了 2026。
        // 如果現在真實年份是 2024 / 2025，這通常表示已經更新了 (或者我們可以單純等 sntp 內部狀態)
        // 最好的做法是：只要時間的回調函數被呼叫，就代表成功了！

        if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED || current_year > 2000)
        {
            // 注意：如果您 NVS 存的是錯的 2026，這裡大於 2000 也會馬上跳出。
            // 所以我們需要強制將系統時間先歸零到 1970！
            sync_success = true;
            break;
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
        retry++;
    }

    if (sync_success)
    {
        ESP_LOGI(TAG, "NTP 時間同步成功！(或已抓到合理年份)");
    }
    else
    {
        ESP_LOGW(TAG, "NTP 時間同步超時或失敗！");
    }
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