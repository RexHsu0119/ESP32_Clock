#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Initialise USB host library and register the MSC class driver.
     *
     * Starts a background FreeRTOS task that manages USB host events.
     * The FATFS volume is mounted at /usb when a mass-storage device is connected.
     *
     * @return ESP_OK on success.
     */
    esp_err_t usb_msc_host_init(void);

    /**
     * @brief Returns true if a USB mass-storage device is currently mounted.
     */
    bool usb_msc_host_is_mounted(void);

    /**
     * @brief Request mounting USB MSC and wait up to timeout.
     *
     * @param timeout_ms wait timeout in milliseconds.
     * @return ESP_OK if mounted, ESP_ERR_TIMEOUT on timeout, ESP_ERR_INVALID_STATE if no USB device connected.
     */
    esp_err_t usb_msc_host_mount(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
