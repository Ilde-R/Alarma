#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "sensor.h"
#include "backend_protocol.h"

#define BACKEND_TAG "BACKEND_PROTOCOL"
#define BACKEND_MSG_MAX 300

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

void backend_protocol_send_pressure(backend_ws_t* ws, int64_t timestamp_ms) {
    if (ws == NULL) return;

    float psi = sensor_get_pressure();
    char payload[BACKEND_MSG_MAX];
    snprintf(payload, sizeof(payload),
             "{\"event\":\"pressure_reading\",\"data\":{\"psi\":%.2f,\"ts\":%llu}}",
             (double) psi, (unsigned long long) timestamp_ms);
    
    ESP_LOGI(BACKEND_TAG, "Enviando JSON (pressure_reading): %s", payload);
             
    if (backend_ws_send_text(ws, payload)) {
        ESP_LOGD(BACKEND_TAG, "Enviado: %s", payload);
    } else {
        ESP_LOGW(BACKEND_TAG, "Fallo al enviar lectura");
    }
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
        const char* value = json_get_number_str(message, "saveIntervalSeconds");
        if (value != NULL) {
            char* end = NULL;
            unsigned long seconds = strtoul(value, &end, 10);
            while (end != NULL && (*end == ' ' || *end == '\t' ||
                                   *end == '\n' || *end == '\r')) {
                end++;
            }
            if (end != value && end != NULL &&
                (*end == ',' || *end == '}') &&
                seconds > 0 && seconds <= UINT32_MAX / 1000U) {
                config->interval_ms = (uint32_t) seconds * 1000U;
                ESP_LOGI(BACKEND_TAG, "NUEVO INTERVALO RECIBIDO: %lu segundos",
                         seconds);
            } else {
                ESP_LOGW(BACKEND_TAG, "Intervalo saveIntervalSeconds invalido; se conserva el actual");
            }
        }

        value = json_get_number_str(message, "scaleFactor");
        if (value != NULL) {
            config->scale = strtof(value, NULL);
            sensor_set_scale(config->scale);
            ESP_LOGI(BACKEND_TAG, "NUEVA ESCALA RECIBIDA: %.4f", (double) config->scale);
        }
        
        backend_config_save(config);
    } else if (strcmp(event, "reading_ack") == 0) {
        ESP_LOGD(BACKEND_TAG, "Lectura confirmada por servidor");
    } else if (strcmp(event, "auth_error") == 0) {
        *credentials_invalid = true;
        backend_ws_close(ws);
        ESP_LOGW(BACKEND_TAG, "AUTH_ERROR: key revocada o invalida");
    }
}