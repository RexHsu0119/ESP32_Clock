idf_component_register(
    SRCS "wifi_portal.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_http_server
        esp_wifi
        esp_event
        esp_netif
        wifi
        wifi_config
)