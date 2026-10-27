idf_component_register(
    SRCS "app_main.cpp"
    INCLUDE_DIRS "."
    REQUIRES HMS_Interface HMS_EspNow esp_wifi esp_timer freertos log
)
