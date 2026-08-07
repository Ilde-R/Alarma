#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "network.h"

#define NVS_NAMESPACE "config"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "pass"
#define NVS_KEY_DEVICE_KEY "device_key"

static const char* TAG = "WIFI_MODULE";

static network_status_t s_status = NETWORK_NEEDS_PROVISIONING;
static bool s_has_creds = false;
static char s_ssid[NETWORK_SSID_MAX_LEN] = "";
static char s_pass[NETWORK_PASS_MAX_LEN] = "";
static char s_device_key[NETWORK_DEVICE_KEY_MAX_LEN] = "";
static char s_ip[NETWORK_IP_STR_LEN] = "0.0.0.0";

static const char* reason_str(uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_NO_AP_FOUND: return "NO_AP_FOUND";
        case WIFI_REASON_AUTH_FAIL: return "AUTH_FAIL (SSID/pass incorrectos)";
        case WIFI_REASON_AUTH_EXPIRE: return "AUTH_EXPIRE (router no responde)";
        case WIFI_REASON_CONNECTION_FAIL: return "CONNECTION_FAIL";
        case WIFI_REASON_HANDSHAKE_TIMEOUT: return "HANDSHAKE_TIMEOUT (pass incorrecta)";
        default: return "OTRO";
    }
}

static esp_err_t nvs_load_credentials(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t len = sizeof(s_ssid);
    err = nvs_get_str(handle, NVS_KEY_SSID, s_ssid, &len);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    len = sizeof(s_pass);
    err = nvs_get_str(handle, NVS_KEY_PASS, s_pass, &len);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    len = sizeof(s_device_key);
    err = nvs_get_str(handle, NVS_KEY_DEVICE_KEY, s_device_key, &len);
    nvs_close(handle);
    if (err != ESP_OK) {
        return err;
    }

    s_has_creds = (s_ssid[0] != '\0' && s_device_key[0] != '\0');
    return ESP_OK;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (s_has_creds) {
            ESP_LOGI(TAG, "Conectando a %s...", s_ssid);
            esp_wifi_connect();
        }
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
        s_status = NETWORK_CONNECTING;
        ESP_LOGW(TAG, "Disconnected from router. Reason: %d (%s). Reconnecting in 5s...", event->reason, reason_str(event->reason));
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_wifi_connect();
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        s_status = NETWORK_CONNECTED;
        snprintf(s_ip, NETWORK_IP_STR_LEN, IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Successfully connected! Assigned IP: %s", s_ip);
    }
}

esp_err_t network_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    nvs_load_credentials();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_OPEN,
        },
    };
    strncpy((char*) wifi_config.sta.ssid, s_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*) wifi_config.sta.password, s_pass, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(34)); // 8.5 dBm: workaround para falla de auth en ESP32-C3 con RF mal sintonizado

    s_status = s_has_creds ? NETWORK_CONNECTING : NETWORK_NEEDS_PROVISIONING;
    if (!s_has_creds) {
        ESP_LOGW(TAG, "Sin credenciales guardadas. Se necesita provisionamiento.");
    }
    return ESP_OK;
}

network_status_t network_get_status(void) {
    return s_status;
}

bool network_is_connected(void) {
    return s_status == NETWORK_CONNECTED;
}

esp_err_t network_get_ip(char* ip_str, size_t max_len) {
    if (ip_str == NULL || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(ip_str, s_ip, max_len - 1);
    ip_str[max_len - 1] = '\0';
    return ESP_OK;
}

const char* network_get_device_key(void) {
    return s_device_key;
}

esp_err_t network_save_credentials(const char* ssid, const char* pass, const char* device_key) {
    if (ssid == NULL || device_key == NULL || ssid[0] == '\0' || device_key[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(handle, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, NVS_KEY_PASS, pass ? pass : "");
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, NVS_KEY_DEVICE_KEY, device_key);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
        strncpy(s_pass, pass ? pass : "", sizeof(s_pass) - 1);
        strncpy(s_device_key, device_key, sizeof(s_device_key) - 1);
        s_has_creds = true;
    }
    return err;
}

esp_err_t network_clear_credentials(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    nvs_erase_key(handle, NVS_KEY_SSID);
    nvs_erase_key(handle, NVS_KEY_PASS);
    nvs_erase_key(handle, NVS_KEY_DEVICE_KEY);
    err = nvs_commit(handle);
    nvs_close(handle);

    s_ssid[0] = '\0';
    s_pass[0] = '\0';
    s_device_key[0] = '\0';
    s_has_creds = false;
    s_status = NETWORK_NEEDS_PROVISIONING;
    return err;
}
