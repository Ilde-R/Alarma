#ifndef NETWORK_H
#define NETWORK_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#define NETWORK_IP_STR_LEN 16
#define NETWORK_SSID_MAX_LEN 33
#define NETWORK_PASS_MAX_LEN 65
#define NETWORK_DEVICE_KEY_MAX_LEN 64

typedef enum {
    NETWORK_NEEDS_PROVISIONING,
    NETWORK_CONNECTING,
    NETWORK_CONNECTED,
} network_status_t;

esp_err_t network_init(void);

network_status_t network_get_status(void);

bool network_is_connected(void);

esp_err_t network_get_ip(char* ip_str, size_t max_len);

const char* network_get_device_key(void);

esp_err_t network_save_credentials(const char* ssid, const char* pass, const char* device_key);

esp_err_t network_clear_credentials(void);

#endif
