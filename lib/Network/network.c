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
#define NVS_KEY_DEVICE_NAME "name"

static const char* TAG = "WIFI_MODULE";

static network_status_t s_status = NETWORK_NEEDS_PROVISIONING;
static bool s_has_creds = false;
static char s_ssid[NETWORK_SSID_MAX_LEN] = "";
static char s_pass[NETWORK_PASS_MAX_LEN] = "";
static char s_device_key[NETWORK_DEVICE_KEY_MAX_LEN] = "";
static char s_device_name[NETWORK_DEVICE_NAME_MAX_LEN] = "";
static char s_ip[NETWORK_IP_STR_LEN] = "0.0.0.0";

static uint32_t s_sta_disconnects = 0;
static uint32_t s_sta_backoff_ms = 2000;
static network_rescue_cb_t s_rescue_cb = NULL;
static bool s_rescue_notified = false;

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
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    len = sizeof(s_device_name);
    err = nvs_get_str(handle, NVS_KEY_DEVICE_NAME, s_device_name, &len);
    nvs_close(handle);
    if (err != ESP_OK) {
        s_device_name[0] = '\0';
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

        s_sta_disconnects++;
        uint32_t backoff = s_sta_backoff_ms;
        s_sta_backoff_ms = s_sta_backoff_ms * 2;
        if (s_sta_backoff_ms > 30000) {
            s_sta_backoff_ms = 30000;
        }

        ESP_LOGW(TAG, "Disconnected from router. Reason: %d (%s). Reconnect %lu. Retrying in %lu ms...",
                 event->reason, reason_str(event->reason),
                 (unsigned long) s_sta_disconnects, (unsigned long) backoff);
        vTaskDelay(pdMS_TO_TICKS(backoff));
        esp_wifi_connect();

        if (s_sta_disconnects >= NETWORK_MAX_STA_DISCONNECTS && !s_rescue_notified) {
            s_rescue_notified = true;
            ESP_LOGW(TAG, "Demasiadas desconexiones (%lu). Abriendo portal de rescate...",
                     (unsigned long) s_sta_disconnects);
            if (s_rescue_cb != NULL) {
                s_rescue_cb();
            }
        }
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        s_status = NETWORK_CONNECTED;
        s_sta_disconnects = 0;
        s_sta_backoff_ms = 2000;
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
    esp_netif_create_default_wifi_ap();

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

const char* network_get_device_name(void) {
    return s_device_name;
}

const char* network_get_ssid(void) {
    return s_ssid;
}

const char* network_get_pass(void) {
    return s_pass;
}

esp_err_t network_save_credentials(const char* ssid, const char* pass, const char* device_key, const char* device_name) {
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
        err = nvs_set_str(handle, NVS_KEY_DEVICE_NAME, device_name ? device_name : "");
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
        strncpy(s_pass, pass ? pass : "", sizeof(s_pass) - 1);
        strncpy(s_device_key, device_key, sizeof(s_device_key) - 1);
        strncpy(s_device_name, device_name ? device_name : "", sizeof(s_device_name) - 1);
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
    nvs_erase_key(handle, NVS_KEY_DEVICE_NAME);
    err = nvs_commit(handle);
    nvs_close(handle);

    s_ssid[0] = '\0';
    s_pass[0] = '\0';
    s_device_key[0] = '\0';
    s_device_name[0] = '\0';
    s_has_creds = false;
    s_status = NETWORK_NEEDS_PROVISIONING;
    return err;
}

esp_err_t network_ap_start(wifi_mode_t mode) {
    wifi_config_t ap_config = {
        .ap = {
            .ssid = NETWORK_AP_SSID,
            .ssid_len = 0,
            .channel = NETWORK_AP_CHANNEL,
            .max_connection = NETWORK_AP_MAX_CONN,
            .authmode = WIFI_AUTH_OPEN,
        },
    };

    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_wifi_set_mode(mode);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }
    esp_wifi_set_max_tx_power(34); // 8.5 dBm: evita fallos de RF en ESP32-C3
    ESP_LOGI(TAG, "AP '%s' iniciado (modo %d). IP 192.168.4.1", NETWORK_AP_SSID, mode);
    return ESP_OK;
}

esp_err_t network_ap_stop(void) {
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "AP desactivado. Modo STA restaurado.");
    return ESP_OK;
}

esp_err_t network_set_rescue_callback(network_rescue_cb_t cb) {
    s_rescue_cb = cb;
    s_rescue_notified = false;
    return ESP_OK;
}
