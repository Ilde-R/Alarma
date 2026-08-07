#ifndef NETWORK_H
#define NETWORK_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_wifi.h"

#define NETWORK_IP_STR_LEN 16
#define NETWORK_SSID_MAX_LEN 33
#define NETWORK_PASS_MAX_LEN 65
#define NETWORK_DEVICE_KEY_MAX_LEN 64
#define NETWORK_DEVICE_NAME_MAX_LEN 33

#define NETWORK_AP_SSID "Alarma_Setup"
#define NETWORK_AP_CHANNEL 1
#define NETWORK_AP_MAX_CONN 4
#define NETWORK_MAX_STA_NETWORK_FAILS 3

typedef enum {
    NETWORK_NEEDS_PROVISIONING,
    NETWORK_CONNECTING,
    NETWORK_CONNECTED,
} network_status_t;

typedef void (*network_rescue_cb_t)(void);

esp_err_t network_init(void);

network_status_t network_get_status(void);

bool network_is_connected(void);

esp_err_t network_get_ip(char* ip_str, size_t max_len);

const char* network_get_device_key(void);

const char* network_get_device_name(void);

const char* network_get_ssid(void);

const char* network_get_pass(void);

esp_err_t network_save_credentials(const char* ssid, const char* pass, const char* device_key, const char* device_name);

esp_err_t network_clear_credentials(void);

esp_err_t network_ap_start(wifi_mode_t mode);

esp_err_t network_ap_stop(void);

esp_err_t network_set_rescue_callback(network_rescue_cb_t cb);

#endif
