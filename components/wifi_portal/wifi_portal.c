#include "wifi_portal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"

#include "lwip/ip4_addr.h"

#include "wifi.h"
#include "wifi_config.h"

static const char *TAG = "WIFI_PORTAL";

static httpd_handle_t s_server = NULL;
static esp_netif_t *s_ap_netif = NULL;
static bool s_running = false;
static bool s_new_credentials = false;
static char s_last_ssid[WIFI_CONFIG_SSID_MAX_LEN + 1] = {0};
static char s_ap_ssid[33] = {0};
static char s_ap_password[65] = {0};

#define SCAN_TASK_STACK_SIZE 6144
#define SCAN_RESPONSE_BUF_SIZE 4096

typedef struct
{
    QueueHandle_t result_queue;
} scan_task_ctx_t;

typedef struct
{
    bool ok;
    char *json;
} scan_task_result_t;

static const char *INDEX_HTML =
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "  <meta charset=\"utf-8\">"
    "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "  <title>ESP32 Clock Wi-Fi Setup</title>"
    "  <style>"
    "    body{font-family:Arial,sans-serif;max-width:520px;margin:24px auto;padding:0 16px;line-height:1.5;background:#fafafa;color:#222;}"
    "    h2{margin-bottom:8px;}"
    "    .box{border:1px solid #ddd;border-radius:10px;padding:16px;background:#fff;box-shadow:0 2px 8px rgba(0,0,0,0.05);}"
    "    input{width:100%;padding:10px;margin:8px 0 12px 0;box-sizing:border-box;border:1px solid #ccc;border-radius:6px;}"
    "    button{padding:10px 16px;font-size:16px;margin-right:8px;border-radius:6px;border:1px solid #bbb;background:#f5f5f5;cursor:pointer;}"
    "    button:hover{background:#eee;}"
    "    .hint{color:#666;font-size:14px;}"
    "    .ap{padding:8px;border:1px solid #ddd;border-radius:6px;margin:6px 0;cursor:pointer;background:#fff;}"
    "    .ap:hover{background:#f5f5f5;}"
    "    .mono{font-family:monospace;}"
    "    .status{margin:12px 0 16px 0;padding:10px;border-radius:8px;background:#f6f8fa;border:1px solid #dde3ea;font-size:14px;}"
    "    .status.ok{background:#edf9f0;border-color:#b7e0c0;color:#176c2f;}"
    "    .status.warn{background:#fff7e6;border-color:#f0d58a;color:#8a5a00;}"
    "    .status.err{background:#fdeeee;border-color:#efb1b1;color:#9b1c1c;}"
    "  </style>"
    "  <script>"
    "    let statusTimer = null;"
    "    let lastScanAps = [];"
    ""
    "    function authText(v){"
    "      switch(v){"
    "        case 0:return 'OPEN';"
    "        case 1:return 'WEP';"
    "        case 2:return 'WPA_PSK';"
    "        case 3:return 'WPA2_PSK';"
    "        case 4:return 'WPA_WPA2_PSK';"
    "        case 5:return 'WPA2_ENTERPRISE';"
    "        case 6:return 'WPA3_PSK';"
    "        case 7:return 'WPA2_WPA3_PSK';"
    "        default:return 'UNKNOWN';"
    "      }"
    "    }"
    ""
    "    function setStatus(text, cls){"
    "      const box=document.getElementById('statusBox');"
    "      box.className='status'+(cls ? ' '+cls : '');"
    "      box.textContent=text;"
    "    }"
    ""
    "    async function refreshStatus(){"
    "      try{"
    "        const resp=await fetch('/status', {cache:'no-store'});"
    "        const data=await resp.json();"
    "        if(data.running && data.new_credentials){"
    "          const ssid = data.ssid && data.ssid.length ? data.ssid : '(unknown)';"
    "          setStatus('Wi-Fi saved, reconnecting to '+ssid+' ...', 'ok');"
    "        }else if(data.running){"
    "          setStatus('Portal Ready', '');"
    "        }else{"
    "          setStatus('Portal is starting...', 'warn');"
    "        }"
    "      }catch(e){"
    "        setStatus('Unable to read portal status', 'warn');"
    "      }"
    "    }"
    ""
    "    function startStatusPolling(){"
    "      if(statusTimer){"
    "        clearInterval(statusTimer);"
    "      }"
    "      statusTimer = setInterval(refreshStatus, 1000);"
    "    }"
    ""
    "    function collapseApList(selectedSsid){"
    "      const list=document.getElementById('aplist');"
    "      list.innerHTML="
    "        '<div class=\"hint\">Selected Wi-Fi: <b>'+selectedSsid+'</b> "
    "        <a href=\"#\" onclick=\"expandApList();return false;\">Change</a></div>';"
    "    }"
    ""
    "    function expandApList(){"
    "      renderApList(lastScanAps);"
    "    }"
    ""
    "    function renderApList(aps){"
    "      const list=document.getElementById('aplist');"
    "      if(!aps || aps.length===0){"
    "        list.innerHTML='<div class=\"hint\">No AP found</div>';"
    "        return;"
    "      }"
    "      list.innerHTML='';"
    "      aps.forEach(ap=>{"
    "        const div=document.createElement('div');"
    "        div.className='ap';"
    "        div.innerHTML='<b>'+ap.ssid+'</b><br><span class=\"hint\">RSSI: '+ap.rssi+' / '+authText(ap.auth)+'</span>';"
    "        div.onclick=function(){"
    "          document.getElementById('ssid').value=ap.ssid;"
    "          setStatus('Selected Wi-Fi: '+ap.ssid, '');"
    "          collapseApList(ap.ssid);"
    "          document.getElementById('password').focus();"
    "        };"
    "        list.appendChild(div);"
    "      });"
    "    }"
    ""
    "    async function scanWifi(){"
    "      const list=document.getElementById('aplist');"
    "      setStatus('Scanning nearby Wi-Fi...', 'warn');"
    "      list.innerHTML='<div class=\"hint\">Scanning...</div>';"
    "      try{"
    "        const resp=await fetch('/scan', {cache:'no-store'});"
    "        const text=await resp.text();"
    "        console.log('/scan response:', text);"
    "        const data=JSON.parse(text);"
    "        if(!data.ok){"
    "          list.innerHTML='<div class=\"hint\">Scan failed</div>';"
    "          setStatus('Scan failed. Please try again.', 'err');"
    "          return;"
    "        }"
    "        if(!data.aps || data.aps.length===0){"
    "          list.innerHTML='<div class=\"hint\">No AP found</div>';"
    "          setStatus('No nearby Wi-Fi found', 'warn');"
    "          return;"
    "        }"
    "        lastScanAps = data.aps;"
    "        renderApList(lastScanAps);"
    "        setStatus('Scan complete. Tap a Wi-Fi name to fill SSID.', 'ok');"
    "      }catch(e){"
    "        console.error('scanWifi error:', e);"
    "        list.innerHTML='<div class=\"hint\">Scan request error</div>';"
    "        setStatus('Scan request error', 'err');"
    "      }"
    "    }"
    ""
    "    async function submitWifi(event){"
    "      event.preventDefault();"
    ""
    "      const ssid=document.getElementById('ssid').value.trim();"
    "      const password=document.getElementById('password').value;"
    ""
    "      if(!ssid){"
    "        setStatus('Please enter SSID', 'err');"
    "        return;"
    "      }"
    ""
    "      setStatus('Saving Wi-Fi settings...', 'warn');"
    ""
    "      try{"
    "        const body='ssid='+encodeURIComponent(ssid)+'&password='+encodeURIComponent(password);"
    "        const resp=await fetch('/connect', {"
    "          method:'POST',"
    "          headers:{'Content-Type':'application/x-www-form-urlencoded'},"
    "          body:body"
    "        });"
    ""
    "        const text=await resp.text();"
    "        console.log('/connect response:', text);"
    ""
    "        if(!resp.ok){"
    "          setStatus('Failed to save Wi-Fi settings', 'err');"
    "          return;"
    "        }"
    ""
    "        setStatus('Wi-Fi saved, reconnecting...', 'ok');"
    "        startStatusPolling();"
    "        refreshStatus();"
    "      }catch(e){"
    "        console.error('submitWifi error:', e);"
    "        setStatus('Save request error', 'err');"
    "      }"
    "    }"
    ""
    "    window.addEventListener('load', function(){"
    "      refreshStatus();"
    "      startStatusPolling();"
    "      const form=document.getElementById('wifiForm');"
    "      form.addEventListener('submit', submitWifi);"
    "    });"
    "  </script>"
    "</head>"
    "<body>"
    "  <div class=\"box\">"
    "    <h2>ESP32 Clock Wi-Fi Setup</h2>"
    "    <p class=\"hint\">Connect this clock to your home Wi-Fi.</p>"
    "    <p class=\"hint mono\">Open: http://192.168.4.1</p>"
    ""
    "    <div id=\"statusBox\" class=\"status\">Portal Ready</div>"
    ""
    "    <button type=\"button\" onclick=\"scanWifi()\">Scan Wi-Fi</button>"
    "    <div id=\"aplist\" style=\"margin:12px 0 16px 0\"></div>"
    ""
    "    <form id=\"wifiForm\">"
    "      <label>SSID</label>"
    "      <input id=\"ssid\" type=\"text\" name=\"ssid\" maxlength=\"32\" required>"
    "      <label>Password</label>"
    "      <input id=\"password\" type=\"password\" name=\"password\" maxlength=\"64\">"
    "      <button type=\"submit\">Save and Connect</button>"
    "    </form>"
    "  </div>"
    "</body>"
    "</html>";

static const char *SUCCESS_HTML =
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "  <meta charset=\"utf-8\">"
    "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "  <title>Saved</title>"
    "  <style>"
    "    body{font-family:Arial,sans-serif;max-width:480px;margin:24px auto;padding:0 16px;line-height:1.5;}"
    "    .ok{border:1px solid #4CAF50;border-radius:8px;padding:16px;}"
    "  </style>"
    "</head>"
    "<body>"
    "  <div class=\"ok\">"
    "    <h2>Wi-Fi Saved</h2>"
    "    <p>Credentials stored successfully.</p>"
    "    <p>Please wait while the device reconnects.</p>"
    "  </div>"
    "</body>"
    "</html>";

static const char *FAIL_HTML =
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "  <meta charset=\"utf-8\">"
    "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "  <title>Failed</title>"
    "  <style>"
    "    body{font-family:Arial,sans-serif;max-width:480px;margin:24px auto;padding:0 16px;line-height:1.5;}"
    "    .fail{border:1px solid #F44336;border-radius:8px;padding:16px;}"
    "  </style>"
    "</head>"
    "<body>"
    "  <div class=\"fail\">"
    "    <h2>Save Failed</h2>"
    "    <p>Unable to save credentials.</p>"
    "    <p>Please go back and try again.</p>"
    "  </div>"
    "</body>"
    "</html>";

static int hex_to_val(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

static void url_decode(const char *src, char *dst, size_t dst_len)
{
    size_t si = 0;
    size_t di = 0;

    if (dst_len == 0)
    {
        return;
    }

    while (src[si] != '\0' && di < dst_len - 1)
    {
        if (src[si] == '+')
        {
            dst[di++] = ' ';
            si++;
        }
        else if (src[si] == '%' &&
                 src[si + 1] != '\0' &&
                 src[si + 2] != '\0')
        {
            int hi = hex_to_val(src[si + 1]);
            int lo = hex_to_val(src[si + 2]);

            if (hi >= 0 && lo >= 0)
            {
                dst[di++] = (char)((hi << 4) | lo);
                si += 3;
            }
            else
            {
                dst[di++] = src[si++];
            }
        }
        else
        {
            dst[di++] = src[si++];
        }
    }

    dst[di] = '\0';
}

static bool form_get_value(const char *body, const char *key, char *out, size_t out_len)
{
    const char *p = body;
    size_t key_len = strlen(key);

    if (body == NULL || key == NULL || out == NULL || out_len == 0)
    {
        return false;
    }

    while (*p != '\0')
    {
        const char *pair_end = strchr(p, '&');
        size_t pair_len = pair_end ? (size_t)(pair_end - p) : strlen(p);

        if (pair_len > key_len + 1 &&
            strncmp(p, key, key_len) == 0 &&
            p[key_len] == '=')
        {
            char encoded[128];
            size_t value_len = pair_len - key_len - 1;

            if (value_len >= sizeof(encoded))
            {
                value_len = sizeof(encoded) - 1;
            }

            memcpy(encoded, p + key_len + 1, value_len);
            encoded[value_len] = '\0';

            url_decode(encoded, out, out_len);
            return true;
        }

        if (pair_end == NULL)
        {
            break;
        }

        p = pair_end + 1;
    }

    return false;
}

static bool json_append_raw(char *buf, size_t buf_size, size_t *offset, const char *text)
{
    if (buf == NULL || offset == NULL || text == NULL)
    {
        return false;
    }

    int written = snprintf(buf + *offset, buf_size - *offset, "%s", text);
    if (written < 0 || (size_t)written >= (buf_size - *offset))
    {
        return false;
    }

    *offset += (size_t)written;
    return true;
}

static bool json_append_escaped(char *buf, size_t buf_size, size_t *offset, const char *text)
{
    if (buf == NULL || offset == NULL || text == NULL)
    {
        return false;
    }

    while (*text != '\0')
    {
        unsigned char c = (unsigned char)*text++;

        switch (c)
        {
        case '\"':
            if (!json_append_raw(buf, buf_size, offset, "\\\""))
                return false;
            break;
        case '\\':
            if (!json_append_raw(buf, buf_size, offset, "\\\\"))
                return false;
            break;
        case '\b':
            if (!json_append_raw(buf, buf_size, offset, "\\b"))
                return false;
            break;
        case '\f':
            if (!json_append_raw(buf, buf_size, offset, "\\f"))
                return false;
            break;
        case '\n':
            if (!json_append_raw(buf, buf_size, offset, "\\n"))
                return false;
            break;
        case '\r':
            if (!json_append_raw(buf, buf_size, offset, "\\r"))
                return false;
            break;
        case '\t':
            if (!json_append_raw(buf, buf_size, offset, "\\t"))
                return false;
            break;
        default:
            if (c < 0x20)
            {
                char esc[8];
                int n = snprintf(esc, sizeof(esc), "\\u%04x", c);
                if (n < 0 || !json_append_raw(buf, buf_size, offset, esc))
                    return false;
            }
            else
            {
                if (*offset + 2 > buf_size)
                    return false;
                buf[*offset] = (char)c;
                (*offset)++;
                buf[*offset] = '\0';
            }
            break;
        }
    }

    return true;
}

static char *build_scan_json_response(wifi_scan_result_t *results, uint16_t count, bool ok)
{
    char *resp = calloc(1, SCAN_RESPONSE_BUF_SIZE);
    if (resp == NULL)
    {
        return NULL;
    }

    size_t off = 0;

    if (!ok)
    {
        snprintf(resp, SCAN_RESPONSE_BUF_SIZE, "{\"ok\":false,\"aps\":[]}");
        return resp;
    }

    if (!json_append_raw(resp, SCAN_RESPONSE_BUF_SIZE, &off, "{\"ok\":true,\"aps\":["))
    {
        free(resp);
        return NULL;
    }

    for (uint16_t i = 0; i < count; i++)
    {
        if (i > 0)
        {
            if (!json_append_raw(resp, SCAN_RESPONSE_BUF_SIZE, &off, ","))
            {
                free(resp);
                return NULL;
            }
        }

        if (!json_append_raw(resp, SCAN_RESPONSE_BUF_SIZE, &off, "{\"ssid\":\""))
        {
            free(resp);
            return NULL;
        }

        if (!json_append_escaped(resp, SCAN_RESPONSE_BUF_SIZE, &off, results[i].ssid))
        {
            free(resp);
            return NULL;
        }

        char tail[96];
        int n = snprintf(tail, sizeof(tail),
                         "\",\"rssi\":%d,\"auth\":%d}",
                         results[i].rssi,
                         (int)results[i].authmode);
        if (n < 0 || !json_append_raw(resp, SCAN_RESPONSE_BUF_SIZE, &off, tail))
        {
            free(resp);
            return NULL;
        }
    }

    if (!json_append_raw(resp, SCAN_RESPONSE_BUF_SIZE, &off, "]}"))
    {
        free(resp);
        return NULL;
    }

    return resp;
}

static void scan_worker_task(void *arg)
{
    scan_task_ctx_t *ctx = (scan_task_ctx_t *)arg;
    scan_task_result_t result = {
        .ok = false,
        .json = NULL,
    };

    if (ctx == NULL || ctx->result_queue == NULL)
    {
        ESP_LOGE(TAG, "scan_worker_task context 無效");
        vTaskDelete(NULL);
        return;
    }

    wifi_scan_result_t *results = calloc(WIFI_SCAN_MAX_APS, sizeof(wifi_scan_result_t));
    if (results == NULL)
    {
        ESP_LOGE(TAG, "配置掃描結果緩衝區失敗");
        result.json = strdup("{\"ok\":false,\"aps\":[]}");
        xQueueSend(ctx->result_queue, &result, portMAX_DELAY);
        free(ctx);
        vTaskDelete(NULL);
        return;
    }

    uint16_t count = WIFI_SCAN_MAX_APS;
    bool ok = wifi_scan_networks(results, &count, WIFI_SCAN_MAX_APS);

    result.ok = ok;
    result.json = build_scan_json_response(results, count, ok);
    if (result.json == NULL)
    {
        ESP_LOGE(TAG, "建立掃描 JSON 回應失敗");
        result.ok = false;
        result.json = strdup("{\"ok\":false,\"aps\":[]}");
    }

    ESP_LOGI(TAG, "scan_worker_task 完成, ok=%d, count=%u", result.ok, ok ? count : 0);

    xQueueSend(ctx->result_queue, &result, portMAX_DELAY);

    free(results);
    free(ctx);
    vTaskDelete(NULL);
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTP GET %s", req->uri);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t scan_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTP GET %s", req->uri);

    QueueHandle_t result_queue = xQueueCreate(1, sizeof(scan_task_result_t));
    if (result_queue == NULL)
    {
        ESP_LOGE(TAG, "建立 scan result queue 失敗");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"aps\":[]}");
        return ESP_OK;
    }

    scan_task_ctx_t *ctx = calloc(1, sizeof(scan_task_ctx_t));
    if (ctx == NULL)
    {
        ESP_LOGE(TAG, "配置 scan task context 失敗");
        vQueueDelete(result_queue);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"aps\":[]}");
        return ESP_OK;
    }

    ctx->result_queue = result_queue;

    BaseType_t ret = xTaskCreatePinnedToCore(scan_worker_task,
                                             "portal_scan",
                                             SCAN_TASK_STACK_SIZE,
                                             ctx,
                                             4,
                                             NULL,
                                             1);
    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "建立 portal_scan task 失敗");
        free(ctx);
        vQueueDelete(result_queue);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"aps\":[]}");
        return ESP_OK;
    }

    scan_task_result_t result;
    memset(&result, 0, sizeof(result));

    if (xQueueReceive(result_queue, &result, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "接收 scan result 失敗");
        vQueueDelete(result_queue);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"aps\":[]}");
        return ESP_OK;
    }

    vQueueDelete(result_queue);

    if (result.json == NULL)
    {
        ESP_LOGE(TAG, "scan result JSON 為 NULL");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"aps\":[]}");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "HTTP GET /scan 回傳長度=%u", (unsigned)strlen(result.json));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, result.json, HTTPD_RESP_USE_STRLEN);

    free(result.json);
    return ESP_OK;
}

static esp_err_t connect_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTP POST %s, content_len=%d", req->uri, req->content_len);

    char body[256] = {0};
    int total_len = req->content_len;
    int received = 0;

    if (total_len <= 0 || total_len >= (int)sizeof(body))
    {
        ESP_LOGW(TAG, "POST /connect 內容長度無效: %d", total_len);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid request length");
        return ESP_FAIL;
    }

    while (received < total_len)
    {
        int ret = httpd_req_recv(req, body + received, total_len - received);
        if (ret <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                ESP_LOGW(TAG, "POST /connect 接收逾時，繼續等待");
                continue;
            }

            ESP_LOGE(TAG, "POST /connect 接收 request body 失敗: %d", ret);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to receive request body");
            return ESP_FAIL;
        }

        received += ret;
    }

    body[received] = '\0';
    ESP_LOGI(TAG, "POST body: %s", body);

    char ssid[WIFI_CONFIG_SSID_MAX_LEN + 1] = {0};
    char password[WIFI_CONFIG_PASSWORD_MAX_LEN + 1] = {0};

    if (!form_get_value(body, "ssid", ssid, sizeof(ssid)))
    {
        ESP_LOGW(TAG, "POST /connect 缺少 ssid 欄位");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing ssid");
        return ESP_FAIL;
    }

    form_get_value(body, "password", password, sizeof(password));

    ESP_LOGI(TAG, "收到 Wi-Fi 設定請求: SSID=%s, password_len=%d",
             ssid, (int)strlen(password));

    if (wifi_config_save_credentials(ssid, password))
    {
        strlcpy(s_last_ssid, ssid, sizeof(s_last_ssid));
        s_new_credentials = true;

        ESP_LOGI(TAG, "Wi-Fi credentials 已儲存，SSID=%s", s_last_ssid);

        httpd_resp_set_type(req, "text/html; charset=utf-8");
        httpd_resp_send(req, SUCCESS_HTML, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "儲存 Wi-Fi credentials 失敗");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, FAIL_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_FAIL;
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTP GET %s", req->uri);

    char resp[160];

    snprintf(resp, sizeof(resp),
             "{\"running\":true,\"new_credentials\":%s,\"ssid\":\"%s\"}",
             s_new_credentials ? "true" : "false",
             s_last_ssid);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t start_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 10;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 8192;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;

    ESP_LOGI(TAG, "準備啟動 HTTP server, port=%d, stack=%d",
             config.server_port, config.stack_size);

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "啟動 HTTP server 失敗: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
        .user_ctx = NULL};

    httpd_uri_t scan = {
        .uri = "/scan",
        .method = HTTP_GET,
        .handler = scan_get_handler,
        .user_ctx = NULL};

    httpd_uri_t connect = {
        .uri = "/connect",
        .method = HTTP_POST,
        .handler = connect_post_handler,
        .user_ctx = NULL};

    httpd_uri_t status = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_get_handler,
        .user_ctx = NULL};

    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &root));
    ESP_LOGI(TAG, "已註冊 URI handler: GET /");

    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &scan));
    ESP_LOGI(TAG, "已註冊 URI handler: GET /scan");

    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &connect));
    ESP_LOGI(TAG, "已註冊 URI handler: POST /connect");

    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &status));
    ESP_LOGI(TAG, "已註冊 URI handler: GET /status");

    ESP_LOGI(TAG, "HTTP server 已啟動");
    return ESP_OK;
}

static void stop_http_server(void)
{
    if (s_server != NULL)
    {
        ESP_LOGI(TAG, "停止 HTTP server");
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "HTTP server 已停止");
    }
}

bool wifi_portal_start(const char *ap_ssid, const char *ap_password)
{
    if (s_running)
    {
        ESP_LOGW(TAG, "wifi_portal 已在運行");
        return true;
    }

    ESP_LOGI(TAG, "開始啟動 Wi-Fi portal");

    if (!wifi_init())
    {
        ESP_LOGE(TAG, "wifi_init 失敗，無法啟動 portal");
        return false;
    }

    wifi_disconnect();

    if (s_ap_netif == NULL)
    {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        if (s_ap_netif == NULL)
        {
            ESP_LOGE(TAG, "建立 AP netif 失敗");
            return false;
        }
        ESP_LOGI(TAG, "已建立預設 AP netif");
    }

    strlcpy(s_ap_ssid,
            (ap_ssid != NULL && strlen(ap_ssid) > 0) ? ap_ssid : WIFI_PORTAL_DEFAULT_AP_SSID,
            sizeof(s_ap_ssid));

    strlcpy(s_ap_password,
            (ap_password != NULL) ? ap_password : WIFI_PORTAL_DEFAULT_AP_PASSWORD,
            sizeof(s_ap_password));

    wifi_config_t ap_config = {0};

    strlcpy((char *)ap_config.ap.ssid, s_ap_ssid, sizeof(ap_config.ap.ssid));
    strlcpy((char *)ap_config.ap.password, s_ap_password, sizeof(ap_config.ap.password));

    ap_config.ap.ssid_len = strlen(s_ap_ssid);
    ap_config.ap.max_connection = 4;
    ap_config.ap.channel = 1;

    if (strlen(s_ap_password) == 0)
    {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    else
    {
        ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_LOGI(TAG, "設定 SoftAP: SSID=%s, password_len=%d, channel=%d",
             s_ap_ssid, (int)strlen(s_ap_password), ap_config.ap.channel);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_ip_info_t ip_info;
    ESP_ERROR_CHECK(esp_netif_get_ip_info(s_ap_netif, &ip_info));

    ESP_LOGI(TAG, "SoftAP 已啟動: SSID=%s", s_ap_ssid);
    ESP_LOGI(TAG, "SoftAP IP: " IPSTR, IP2STR(&ip_info.ip));
    ESP_LOGI(TAG, "Portal IP: %s", WIFI_PORTAL_DEFAULT_AP_IP);

    if (start_http_server() != ESP_OK)
    {
        esp_wifi_stop();
        return false;
    }

    s_new_credentials = false;
    s_running = true;

    ESP_LOGI(TAG, "wifi_portal 啟動完成");
    return true;
}

void wifi_portal_stop(void)
{
    if (!s_running)
    {
        return;
    }

    ESP_LOGI(TAG, "停止 wifi_portal");

    stop_http_server();

    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT)
    {
        ESP_LOGW(TAG, "停止 Wi-Fi 失敗: %s", esp_err_to_name(err));
    }

    s_running = false;
    ESP_LOGI(TAG, "wifi_portal 已停止");
}

bool wifi_portal_is_running(void)
{
    return s_running;
}

bool wifi_portal_has_new_credentials(void)
{
    return s_new_credentials;
}

void wifi_portal_clear_new_credentials_flag(void)
{
    s_new_credentials = false;
}

const char *wifi_portal_get_ap_ip(void)
{
    return WIFI_PORTAL_DEFAULT_AP_IP;
}

const char *wifi_portal_get_last_ssid(void)
{
    return s_last_ssid;
}