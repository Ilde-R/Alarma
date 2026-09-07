idf_component_register(
    SRCS "provision.c"
    INCLUDE_DIRS "include"
    REQUIRES network esp_http_server esp_wifi freertos log
)
