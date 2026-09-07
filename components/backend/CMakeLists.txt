idf_component_register(
    SRCS "backend.c" "backend_config.c" "backend_ws.c" "backend_protocol.c"
    INCLUDE_DIRS "include"
    REQUIRES network provision sensor ina219 tcp_transport esp-tls esp_wifi esp_timer nvs_flash freertos log
)
