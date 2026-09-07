#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "ina219.h"
#include "network.h"
#include "sensor.h"
#include "backend_protocol.h"

#define BACKEND_TAG "BACKEND_PROTOCOL"
#define BACKEND_MSG_MAX 300
#define BACKEND_FW_VERSION "1.0.0"

static bool json_get_string(const char* json, const char* key, char* out, size_t out_len) {
    if (json == NULL || key == NULL || out == NULL || out_len == 0) {
        return false;
    }
    size_t key_len = strlen(key);
    const char* p = json;
    while ((p = strstr(p, "\"")) != NULL) {
        const char* key_start = p + 1;
        if (strncmp(key_start, key, key_len) == 0 && key_start[key_len] == '"') {
            const char* value = key_start + key_len + 1;
            while (*value == ' ' || *value == '\t' || *value == '\n' || *value == '\r') value++;
            if (*value++ != ':') {
                p = key_start;
                continue;
            }
            while (*value == ' ' || *value == '\t' || *value == '\n' || *value == '\r') value++;
            if (*value++ != '"') {
                p = key_start;
                continue;
            }
            size_t index = 0;
            while (*value != '\0' && *value != '"') {
                if (*value == '\\' && value[1] != '\0') {
                    value++;
                }
                if (index + 1 < out_len) {
                    out[index++] = *value;
                }
                value++;
            }
            if (*value == '"') {
                out[index] = '\0';
                return true;
            }
            return false;
        }
        p = key_start;
    }
    return false;
}

static const char* json_get_number_str(const char* json, const char* key) {
    size_t key_len = strlen(key);
    const char* p = json;
    while ((p = strstr(p, "\"")) != NULL) {
        const char* key_start = p + 1;
        if (strncmp(key_start, key, key_len) == 0 && key_start[key_len] == '"') {
            const char* value = key_start + key_len + 1;
            while (*value == ' ' || *value == '\t' || *value == '\n' || *value == '\r') value++;
            if (*value++ == ':') {
                while (*value == ' ' || *value == '\t' || *value == '\n' || *value == '\r') value++;
                return value;
            }
        }
        p = key_start;
    }
    return NULL;
}

static void json_escape(const char* input, char* output, size_t output_length) {
    if (output_length == 0) {
        return;
    }
    size_t out = 0;
    for (const unsigned char* current = (const unsigned char*) input;
         *current != '\0' && out + 1 < output_length; current++) {
        if (*current == '"' || *current == '\\') {
            if (out + 2 >= output_length) break;
            output[out++] = '\\';
        }
        output[out++] = (char) *current;
    }
    output[out] = '\0';
}

void backend_protocol_send_pressure(backend_ws_t* ws, int64_t timestamp_ms) {
    float psi = sensor_get_pressure();
    ina219_reading_t electrical = {0};
    char payload[BACKEND_MSG_MAX];
    if (ina219_read(&electrical) == ESP_OK) {
        snprintf(payload, sizeof(payload),
                 "{\"event\":\"pressure_reading\",\"data\":{\"psi\":%.2f,\"current_mA\":%.2f,\"voltage_V\":%.3f,\"power_mW\":%.2f,\"ts\":%llu}}",
                 (double) psi, (double) electrical.current_ma,
                 (double) electrical.bus_voltage_v, (double) electrical.power_mw,
                 (unsigned long long) timestamp_ms);
    } else {
        snprintf(payload, sizeof(payload),
                 "{\"event\":\"pressure_reading\",\"data\":{\"psi\":%.2f,\"ts\":%llu}}",
                 (double) psi, (unsigned long long) timestamp_ms);
    }
    if (backend_ws_send_text(ws, payload)) {
        ESP_LOGI(BACKEND_TAG, "Enviado: %s", payload);
    } else {
        ESP_LOGW(BACKEND_TAG, "Fallo al enviar lectura");
    }
}

void backend_protocol_send_device_info(backend_ws_t* ws, const char* device_key) {
    wifi_ap_record_t ap_info;
    int rssi = 0;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
    }
    char key_escaped[NETWORK_DEVICE_KEY_MAX_LEN * 2 + 1];
    char name_escaped[NETWORK_DEVICE_NAME_MAX_LEN * 2 + 1];
    char ssid_escaped[NETWORK_SSID_MAX_LEN * 2 + 1];
    json_escape(device_key, key_escaped, sizeof(key_escaped));
    json_escape(network_get_device_name(), name_escaped, sizeof(name_escaped));
    json_escape(network_get_ssid(), ssid_escaped, sizeof(ssid_escaped));

    char payload[2048];
    snprintf(payload, sizeof(payload),
             "{\"event\":\"device_info\",\"data\":{\"deviceKey\":\"%s\",\"name\":\"%s\",\"ssid\":\"%s\",\"firmware\":\"%s\",\"rssi\":%d,\"uptime\":%llu,\"heap\":%d}}",
             key_escaped, name_escaped, ssid_escaped, BACKEND_FW_VERSION, rssi,
             (unsigned long long) (esp_timer_get_time() / 1000),
             (int) esp_get_free_heap_size());
    backend_ws_send_text(ws, payload);
}

void backend_protocol_handle_message(backend_ws_t* ws, const char* message,
                                     backend_config_t* config,
                                     bool* credentials_invalid) {
    char event[32];
    if (config == NULL || credentials_invalid == NULL ||
        !json_get_string(message, "event", event, sizeof(event))) {
        return;
    }

    if (strcmp(event, "update_threshold") == 0 || strcmp(event, "current_threshold") == 0) {
        const char* value = json_get_number_str(message, "threshold");
        if (value != NULL) {
            config->threshold = strtof(value, NULL);
            sensor_set_threshold(config->threshold);
            backend_config_save(config);
            ESP_LOGI(BACKEND_TAG, "NUEVO UMBRAL RECIBIDO: %.2f", (double) config->threshold);
        }
    } else if (strcmp(event, "device_config_update") == 0) {
        const char* value = json_get_number_str(message, "readIntervalMs");
        if (value != NULL) {
            config->interval_ms = (uint32_t) strtoul(value, NULL, 10);
        }
        value = json_get_number_str(message, "scaleFactor");
        if (value != NULL) {
            config->scale = strtof(value, NULL);
            sensor_set_scale(config->scale);
        }
        backend_config_save(config);
        backend_ws_send_text(ws, "{\"event\":\"config_ack\",\"data\":{\"ok\":true}}");
    } else if (strcmp(event, "reading_ack") == 0) {
        ESP_LOGI(BACKEND_TAG, "Lectura confirmada por servidor");
    } else if (strcmp(event, "auth_error") == 0) {
        *credentials_invalid = true;
        backend_ws_close(ws);
        ESP_LOGW(BACKEND_TAG, "AUTH_ERROR: key revocada o invalida");
    }
}
