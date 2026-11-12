idf_component_register(
    SRCS "app_main.cpp"
    INCLUDE_DIRS "."
    REQUIRES HMS_EspNow nvs_flash esp_wifi esp_event esp_netif log freertos
)
