idf_component_register(
    SRCS "hl100d.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_adc log freertos 
)