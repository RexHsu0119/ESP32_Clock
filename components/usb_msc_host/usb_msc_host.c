#include "usb_msc_host.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"

#include "usb/usb_host.h"
#include "usb/msc_host.h"
#include "usb/msc_host_vfs.h"

#define TAG "USB_MSC"

#define USB_MSC_MOUNT_POINT "/usb"
#define USB_HOST_TASK_STACK 4096
#define USB_CLIENT_TASK_STACK 4096
#define USB_HOST_TASK_PRIORITY 5
#define USB_CLIENT_TASK_PRIORITY 5
#define USB_TASK_CORE_ID 1
#define USB_MSC_INSTALL_SETTLE_DELAY_MS 1200
#define USB_MSC_INSTALL_RETRY_COUNT 8
#define USB_MSC_INSTALL_RETRY_DELAY_MS 1000

#define BIT_DEV_CONNECTED BIT0
#define BIT_DEV_GONE BIT1
#define BIT_HOST_LIB_DONE BIT2
#define BIT_MOUNT_REQUEST BIT3

static msc_host_device_handle_t s_msc_device = NULL;
static msc_host_vfs_handle_t s_vfs_handle = NULL;
static EventGroupHandle_t s_usb_events = NULL;
static uint8_t s_pending_device_address = 0;
static bool s_mounted = false;
static bool s_device_connected = false;

static void usb_msc_cleanup_current_device(void)
{
    s_mounted = false;

    if (s_vfs_handle != NULL)
    {
        msc_host_vfs_unregister(s_vfs_handle);
        s_vfs_handle = NULL;
        ESP_LOGI(TAG, "[DEBUG] VFS unregistered");
    }

    if (s_msc_device != NULL)
    {
        msc_host_uninstall_device(s_msc_device);
        s_msc_device = NULL;
        ESP_LOGI(TAG, "[DEBUG] Device uninstalled");
    }
}

static esp_err_t usb_msc_try_mount_pending_device(void)
{
    if (!s_device_connected || s_pending_device_address == 0)
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t install_ret = ESP_FAIL;

    /* Ensure stale handles are released before mounting a new device. */
    usb_msc_cleanup_current_device();

    /* Some flash drives need extra time after connect before SCSI inquiry works reliably. */
    vTaskDelay(pdMS_TO_TICKS(USB_MSC_INSTALL_SETTLE_DELAY_MS));

    ESP_LOGI(TAG, "[DEBUG] Installing device at address %u", s_pending_device_address);
    for (int attempt = 1; attempt <= USB_MSC_INSTALL_RETRY_COUNT; attempt++)
    {
        install_ret = msc_host_install_device(s_pending_device_address, &s_msc_device);
        if (install_ret == ESP_OK)
        {
            break;
        }

        ESP_LOGW(TAG,
                 "[DEBUG] msc_host_install_device failed (attempt %d/%d) addr=%u err=%s",
                 attempt,
                 USB_MSC_INSTALL_RETRY_COUNT,
                 s_pending_device_address,
                 esp_err_to_name(install_ret));

        /* Ensure partial state from failed install is gone before next attempt. */
        usb_msc_cleanup_current_device();

        if (attempt < USB_MSC_INSTALL_RETRY_COUNT)
        {
            vTaskDelay(pdMS_TO_TICKS(USB_MSC_INSTALL_RETRY_DELAY_MS));
        }
    }

    if (install_ret != ESP_OK)
    {
        ESP_LOGE(TAG, "[DEBUG] msc_host_install_device FAILED for addr %u after retries", s_pending_device_address);
        return install_ret;
    }

    ESP_LOGI(TAG, "[DEBUG] Device installed, attempting VFS mount at %s", USB_MSC_MOUNT_POINT);
    msc_host_vfs_handle_t vfs_h = NULL;
    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 4096,
    };

    esp_err_t ret = msc_host_vfs_register(s_msc_device,
                                          USB_MSC_MOUNT_POINT,
                                          &mount_config,
                                          &vfs_h);
    if (ret == ESP_OK)
    {
        s_vfs_handle = vfs_h;
        s_mounted = true;
        ESP_LOGI(TAG, "[DEBUG] ✓ USB drive MOUNTED at %s", USB_MSC_MOUNT_POINT);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "[DEBUG] ✗ VFS mount FAILED: %s (0x%x)", esp_err_to_name(ret), ret);
    usb_msc_cleanup_current_device();
    return ret;
}

/* ---------------------------------------------------------------------- */
/*  MSC class driver callback                                              */
/* ---------------------------------------------------------------------- */

static void msc_event_cb(const msc_host_event_t *event, void *arg)
{
    (void)arg;

    if (event == NULL || s_usb_events == NULL)
    {
        return;
    }

    if (event->event == MSC_DEVICE_CONNECTED)
    {
        s_device_connected = true;
        s_pending_device_address = event->device.address;
        ESP_LOGI(TAG, "[DEBUG] MSC device CONNECTED (addr %u)", event->device.address);
        xEventGroupSetBits(s_usb_events, BIT_DEV_CONNECTED);
    }
    else if (event->event == MSC_DEVICE_DISCONNECTED)
    {
        s_device_connected = false;
        ESP_LOGI(TAG, "[DEBUG] MSC device DISCONNECTED");
        xEventGroupSetBits(s_usb_events, BIT_DEV_GONE);
    }
    else
    {
        ESP_LOGW(TAG, "[DEBUG] Unknown MSC event: %d", event->event);
    }
}

/* ---------------------------------------------------------------------- */
/*  USB host library daemon task                                           */
/*  Handles host-level events (port state, new/removed clients, etc.)     */
/* ---------------------------------------------------------------------- */

static void usb_host_daemon_task(void *arg)
{
    bool has_clients = true;
    bool has_devices = true;

    while (has_clients || has_devices)
    {
        uint32_t flags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);

        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
        {
            has_clients = false;
        }
        if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)
        {
            has_devices = false;
        }
    }

    ESP_LOGI(TAG, "USB host library is now free");
    xEventGroupSetBits(s_usb_events, BIT_HOST_LIB_DONE);
    vTaskDelete(NULL);
}

/* ---------------------------------------------------------------------- */
/*  MSC client task                                                        */
/*  Mounts / unmounts the FATFS volume as devices connect / disconnect.   */
/* ---------------------------------------------------------------------- */

static void usb_msc_client_task(void *arg)
{
    while (1)
    {
        EventBits_t bits = xEventGroupWaitBits(s_usb_events,
                                               BIT_DEV_CONNECTED | BIT_DEV_GONE | BIT_MOUNT_REQUEST,
                                               pdTRUE,  /* clear on exit */
                                               pdFALSE, /* wait for any */
                                               portMAX_DELAY);

        if (bits & BIT_DEV_CONNECTED)
        {
            ESP_LOGI(TAG, "[DEBUG] Device connected; waiting for explicit mount request");
        }

        if (bits & BIT_MOUNT_REQUEST)
        {
            if (s_mounted)
            {
                ESP_LOGI(TAG, "[DEBUG] Mount request ignored; already mounted");
            }
            else
            {
                esp_err_t mount_ret = usb_msc_try_mount_pending_device();
                if (mount_ret != ESP_OK)
                {
                    ESP_LOGW(TAG, "[DEBUG] Mount request failed: %s", esp_err_to_name(mount_ret));
                }
            }
        }

        if (bits & BIT_DEV_GONE)
        {
            ESP_LOGI(TAG, "[DEBUG] Device disconnection detected, unmounting...");
            usb_msc_cleanup_current_device();

            ESP_LOGI(TAG, "[DEBUG] ✗ USB drive UNMOUNTED");
        }
    }
}

/* ---------------------------------------------------------------------- */
/*  Public API                                                             */
/* ---------------------------------------------------------------------- */

esp_err_t usb_msc_host_init(void)
{
    esp_err_t ret;

    s_usb_events = xEventGroupCreate();
    if (s_usb_events == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    /* Install USB host library */
    const usb_host_config_t host_cfg = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ret = usb_host_install(&host_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "usb_host_install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Start host daemon task */
    BaseType_t xret = xTaskCreatePinnedToCore(usb_host_daemon_task,
                                              "usb_daemon",
                                              USB_HOST_TASK_STACK,
                                              NULL,
                                              USB_HOST_TASK_PRIORITY,
                                              NULL,
                                              USB_TASK_CORE_ID);
    if (xret != pdPASS)
    {
        return ESP_ERR_NO_MEM;
    }

    /* Install MSC class driver */
    const msc_host_driver_config_t msc_cfg = {
        .create_backround_task = true,
        .task_priority = USB_CLIENT_TASK_PRIORITY,
        .stack_size = USB_CLIENT_TASK_STACK,
        .core_id = USB_TASK_CORE_ID,
        .callback = msc_event_cb,
        .callback_arg = NULL,
    };
    ret = msc_host_install(&msc_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "msc_host_install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Start mount management task */
    xret = xTaskCreatePinnedToCore(usb_msc_client_task,
                                   "usb_msc",
                                   USB_CLIENT_TASK_STACK,
                                   NULL,
                                   USB_CLIENT_TASK_PRIORITY,
                                   NULL,
                                   USB_TASK_CORE_ID);
    if (xret != pdPASS)
    {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

bool usb_msc_host_is_mounted(void)
{
    if (s_mounted)
    {
        ESP_LOGD(TAG, "[DEBUG] USB mounted check: YES");
    }
    else
    {
        ESP_LOGD(TAG, "[DEBUG] USB mounted check: NO");
    }
    return s_mounted;
}

esp_err_t usb_msc_host_mount(uint32_t timeout_ms)
{
    if (s_mounted)
    {
        return ESP_OK;
    }

    if (!s_device_connected)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_usb_events == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    xEventGroupSetBits(s_usb_events, BIT_MOUNT_REQUEST);

    TickType_t start = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    while (!s_mounted)
    {
        if (!s_device_connected)
        {
            return ESP_ERR_INVALID_STATE;
        }

        TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed >= timeout_ticks)
        {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    return ESP_OK;
}
