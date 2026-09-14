idf_component_register(
    SRCS "sensor.c"
    INCLUDE_DIRS "include"
    REQUIRES hl100d led esp_driver_gpio freertos log esp_adc
)