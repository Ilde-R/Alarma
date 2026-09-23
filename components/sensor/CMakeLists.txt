idf_component_register(
    SRCS "sensor.c"
    INCLUDE_DIRS "include"
    REQUIRES xgzp6847a led esp_driver_gpio freertos log esp_adc
)