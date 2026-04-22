idf_component_register(
    SRCS "usb_msc_host.c"
    INCLUDE_DIRS "."
    REQUIRES usb freertos esp_timer
)
