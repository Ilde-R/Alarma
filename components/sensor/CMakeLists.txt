idf_component_register(
    SRCS "sensor.c"
    INCLUDE_DIRS "include"
    REQUIRES hx711 led esp_driver_gpio freertos log
)
