idf_component_register(
    SRCS "alarm_logic.c" "calendar_logic.c" "app_state.c" "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_timer
        driver
        display
        button
        rtc
        wifi
        weather
        wifi_config
        wifi_portal
        lvgl
)