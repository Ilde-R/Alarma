idf_component_register(
    SRCS "backend.c"
    INCLUDE_DIRS "include"
    REQUIRES network provision sensor tcp_transport esp-tls esp_wifi esp_timer nvs_flash freertos log
)
