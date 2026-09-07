idf_component_register(
    SRCS "main.c"
    REQUIRES backend network provision sensor ina219 freertos log
)
