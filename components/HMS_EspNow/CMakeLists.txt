idf_component_register(
    SRCS "src/HMS_EspNow.cpp" "src/HMS_EspNowNode.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_common freertos log pthread HMS_Interface
)
