idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES wifi rtc display button nvs_flash lvgl esp_timer
)