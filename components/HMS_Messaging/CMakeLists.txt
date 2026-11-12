idf_component_register(
    SRCS "src/HMS_Messaging.cpp"
    INCLUDE_DIRS "include"
    REQUIRES HMS_Interface freertos pthread log esp_hw_support
)
