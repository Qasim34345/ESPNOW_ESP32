idf_component_register(
    SRCS "src/HMS_EspNow.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi freertos log esp_common HMS_Interface
)
