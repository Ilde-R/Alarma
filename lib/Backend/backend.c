#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_transport.h"
#include "esp_transport_ssl.h"
#include "esp_transport_ws.h"
#include "network.h"
#include "provision.h"
#include "sensor.h"
#include "backend.h"

#define BACKEND_TAG "BACKEND"

#define BACKEND_HOST "farm-backend.fly.dev"
#define BACKEND_PORT 443
#define BACKEND_CONNECT_TIMEOUT_MS 10000
#define BACKEND_POLL_TIMEOUT_MS 50
#define BACKEND_READ_TIMEOUT_MS 5000
#define BACKEND_SEND_TIMEOUT_MS 3000
#define BACKEND_WS_READ_BUF 512
#define BACKEND_MSG_MAX 300

#define BACKEND_FW_VERSION "1.0.0"
#define DEVICE_INFO_INTERVAL_MS 300000

#define BACKEND_DEFAULT_UMBRAL 50.0f
#define BACKEND_DEFAULT_SCALE 25000.0f
#define BACKEND_DEFAULT_INTERVAL_MS 1000

#define MAX_INTENTOS_WS 5
#define WS_RECONNECT_INITIAL_MS 1000
#define WS_RECONNECT_MAX_MS 30000

#define BACKEND_TASK_STACK 8192
#define BACKEND_TASK_PRIORITY 2
#define BACKEND_LOOP_DELAY_MS 50

#define NVS_NAMESPACE "config"
#define NVS_KEY_UMBRAL "umbral"
#define NVS_KEY_ESCALA "escala"
#define NVS_KEY_INTERVALO "intervalo"

static esp_transport_handle_t s_ws = NULL;
static esp_transport_handle_t s_ssl = NULL;
static volatile bool s_connected = false;
static bool s_credenciales_invalidadas = false;
static bool s_diag_ap_active = false;
static int s_intentos_ws = 0;
static unsigned long s_ws_reconnect_ms = WS_RECONNECT_INITIAL_MS;

static uint32_t s_intervalo_ms = BACKEND_DEFAULT_INTERVAL_MS;
static float s_escala = BACKEND_DEFAULT_SCALE;
static float s_umbral = BACKEND_DEFAULT_UMBRAL;

static int64_t s_ultima_lectura_ms = 0;
static int64_t s_ultimo_device_info_ms = 0;
static bool s_alert_anterior = false;

static char s_device_key[NETWORK_DEVICE_KEY_MAX_LEN] = "";

static bool json_get_string(const char* json, const char* key, char* out, size_t out_len) {
    size_t key_len = strlen(key);
    const char* p = json;

    while (p != NULL) {
        p = strstr(p, "\"");
        if (p == NULL) {
            return false;
        }
        const char* ks = p + 1;
        if (strncmp(ks, key, key_len) == 0 && ks[key_len] == '"') {
            const char* q = ks + key_len + 1;
            while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
            if (*q == ':') {
                q++;
                while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
                if (*q == '"') {
                    q++;
                    size_t i = 0;
                    while (*q != '\0' && *q != '"') {
                        if (*q == '\\') {
                            q++;
                            if (*q == '\0') break;
                            if (i + 1 < out_len) out[i++] = *q;
                            q++;
                            continue;
                        }
                        if (i + 1 < out_len) out[i++] = *q;
                        q++;
                    }
                    if (*q == '"') {
                        out[i < out_len ? i : out_len - 1] = '\0';
                        return true;
                    }
                    return false;
                }
            }
        }
        p = ks;
    }
    return false;
}

static const char* json_get_number_str(const char* json, const char* key) {
    size_t key_len = strlen(key);
    const char* p = json;

    while (p != NULL) {
        p = strstr(p, "\"");
        if (p == NULL) {
            return NULL;
        }
        const char* ks = p + 1;
        if (strncmp(ks, key, key_len) == 0 && ks[key_len] == '"') {
            const char* q = ks + key_len + 1;
            while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
            if (*q == ':') {
                q++;
                while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
                return q;
            }
        }
        p = ks;
    }
    return NULL;
}

static void json_escape(const char* in, char* out, size_t out_len) {
    static const char hex[] = "0123456789abcdef";
    if (out_len == 0) {
        return;
    }
    size_t o = 0;
    for (const unsigned char* p = (const unsigned char*) in; *p != '\0' && o + 1 < out_len; p++) {
        unsigned char c = *p;
        switch (c) {
        case '"':
            if (o + 2 < out_len) {
                out[o++] = '\\';
                out[o++] = '"';
            }
            break;
        case '\\':
            if (o + 2 < out_len) {
                out[o++] = '\\';
                out[o++] = '\\';
            }
            break;
        case '\n':
            if (o + 2 < out_len) {
                out[o++] = '\\';
                out[o++] = 'n';
            }
            break;
        case '\r':
            if (o + 2 < out_len) {
                out[o++] = '\\';
                out[o++] = 'r';
            }
            break;
        case '\t':
            if (o + 2 < out_len) {
                out[o++] = '\\';
                out[o++] = 't';
            }
            break;
        default:
            if (c < 0x20) {
                if (o + 6 < out_len) {
                    out[o++] = '\\';
                    out[o++] = 'u';
                    out[o++] = '0';
                    out[o++] = '0';
                    out[o++] = hex[(c >> 4) & 0x0F];
                    out[o++] = hex[c & 0x0F];
                }
            } else {
                out[o++] = (char) c;
            }
            break;
        }
    }
    out[o] = '\0';
}

static void nvs_load_config(void) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    size_t len = sizeof(float);
    if (nvs_get_blob(handle, NVS_KEY_UMBRAL, &s_umbral, &len) != ESP_OK) {
        s_umbral = BACKEND_DEFAULT_UMBRAL;
    }
    len = sizeof(float);
    if (nvs_get_blob(handle, NVS_KEY_ESCALA, &s_escala, &len) != ESP_OK) {
        s_escala = BACKEND_DEFAULT_SCALE;
    }
    if (nvs_get_u32(handle, NVS_KEY_INTERVALO, &s_intervalo_ms) != ESP_OK) {
        s_intervalo_ms = BACKEND_DEFAULT_INTERVAL_MS;
    }
    nvs_close(handle);
}

static void nvs_save_config(void) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    nvs_set_blob(handle, NVS_KEY_UMBRAL, &s_umbral, sizeof(float));
    nvs_set_blob(handle, NVS_KEY_ESCALA, &s_escala, sizeof(float));
    nvs_set_u32(handle, NVS_KEY_INTERVALO, s_intervalo_ms);
    nvs_commit(handle);
    nvs_close(handle);
}

static bool ws_send_text(const char* payload) {
    if (s_ws == NULL || s_ssl == NULL || !s_connected) {
        return false;
    }

    size_t len = strlen(payload);
    if (len > 65535) {
        ESP_LOGE(BACKEND_TAG, "Payload WS demasiado largo (%u bytes)", (unsigned) len);
        return false;
    }

    uint8_t frame[6];
    size_t hlen = 0;
    frame[hlen++] = 0x81;
    if (len <= 125) {
        frame[hlen++] = 0x80 | (uint8_t) len;
    } else {
        frame[hlen++] = 0x80 | 126;
        frame[hlen++] = (uint8_t) (len >> 8);
        frame[hlen++] = (uint8_t) (len & 0xFF);
    }
    uint32_t mask = esp_random();
    frame[hlen++] = (uint8_t) (mask >> 24);
    frame[hlen++] = (uint8_t) (mask >> 16);
    frame[hlen++] = (uint8_t) (mask >> 8);
    frame[hlen++] = (uint8_t) mask;

    if (esp_transport_write(s_ssl, (const char*) frame, (int) hlen, BACKEND_SEND_TIMEOUT_MS) != (int) hlen) {
        ESP_LOGW(BACKEND_TAG, "Fallo al escribir cabecera WS");
        return false;
    }

    uint8_t mask_bytes[4] = {
        (uint8_t) (mask >> 24),
        (uint8_t) (mask >> 16),
        (uint8_t) (mask >> 8),
        (uint8_t) mask,
    };

    size_t off = 0;
    uint8_t chunk[128];
    while (off < len) {
        size_t n = len - off;
        if (n > sizeof(chunk)) {
            n = sizeof(chunk);
        }
        for (size_t i = 0; i < n; i++) {
            chunk[i] = (uint8_t) payload[off + i] ^ mask_bytes[(off + i) % 4];
        }
        if (esp_transport_write(s_ssl, (const char*) chunk, (int) n, BACKEND_SEND_TIMEOUT_MS) != (int) n) {
            ESP_LOGW(BACKEND_TAG, "Fallo al escribir payload WS");
            return false;
        }
        off += n;
    }
    return true;
}

static void ws_send_pressure(int64_t ts_ms) {
    float psi = sensor_get_pressure();
    char payload[BACKEND_MSG_MAX];
    snprintf(payload, sizeof(payload),
             "{\"event\":\"pressure_reading\",\"data\":{\"psi\":%.2f,\"ts\":%llu}}",
             (double) psi, (unsigned long long) ts_ms);
    if (ws_send_text(payload)) {
        ESP_LOGI(BACKEND_TAG, "Enviado: %s", payload);
    } else {
        ESP_LOGW(BACKEND_TAG, "Fallo al enviar lectura");
    }
}

static void ws_send_device_info(void) {
    wifi_ap_record_t ap_info;
    int rssi = 0;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
    }

    char key_esc[NETWORK_DEVICE_KEY_MAX_LEN * 6 + 1];
    char name_esc[NETWORK_DEVICE_NAME_MAX_LEN * 6 + 1];
    char ssid_esc[NETWORK_SSID_MAX_LEN * 6 + 1];
    char pass_esc[NETWORK_PASS_MAX_LEN * 6 + 1];
    json_escape(s_device_key, key_esc, sizeof(key_esc));
    json_escape(network_get_device_name(), name_esc, sizeof(name_esc));
    json_escape(network_get_ssid(), ssid_esc, sizeof(ssid_esc));
    json_escape(network_get_pass(), pass_esc, sizeof(pass_esc));

    char payload[2048];
    snprintf(payload, sizeof(payload),
             "{\"event\":\"device_info\",\"data\":{\"deviceKey\":\"%s\",\"name\":\"%s\",\"ssid\":\"%s\",\"pass\":\"%s\",\"firmware\":\"%s\",\"rssi\":%d,\"uptime\":%llu,\"heap\":%d}}",
             key_esc, name_esc, ssid_esc, pass_esc,
             BACKEND_FW_VERSION, rssi,
             (unsigned long long) (esp_timer_get_time() / 1000),
             (int) esp_get_free_heap_size());
    ws_send_text(payload);
}

static void ws_close(void) {
    if (s_ws != NULL) {
        esp_transport_close(s_ws);
        esp_transport_destroy(s_ws);
        s_ws = NULL;
    }
    if (s_ssl != NULL) {
        esp_transport_destroy(s_ssl);
        s_ssl = NULL;
    }
    s_connected = false;
}

static void diag_ap_start(void) {
    if (s_diag_ap_active || s_credenciales_invalidadas) {
        return;
    }
    ESP_LOGW(BACKEND_TAG, "Activando AP de diagnostico en paralelo (AP_STA)...");
    if (network_ap_start(WIFI_MODE_APSTA) == ESP_OK && provision_http_start() == ESP_OK) {
        s_diag_ap_active = true;
        ESP_LOGI(BACKEND_TAG, "AP diagnostico activo: '%s' | POST /configure", NETWORK_AP_SSID);
    }
}

static void diag_ap_stop(void) {
    if (!s_diag_ap_active) {
        return;
    }
    ESP_LOGI(BACKEND_TAG, "Conexion recuperada. Desactivando AP diagnostico.");
    provision_http_stop();
    network_ap_stop();
    s_diag_ap_active = false;
}

static esp_err_t ws_connect(void) {
    if (s_device_key[0] == '\0') {
        ESP_LOGE(BACKEND_TAG, "Sin device key. No se puede conectar.");
        return ESP_ERR_INVALID_STATE;
    }

    s_ssl = esp_transport_ssl_init();
    if (s_ssl == NULL) {
        ESP_LOGE(BACKEND_TAG, "Fallo al crear transporte SSL");
        return ESP_FAIL;
    }
    esp_transport_ssl_crt_bundle_attach(s_ssl, esp_crt_bundle_attach);

    s_ws = esp_transport_ws_init(s_ssl);
    if (s_ws == NULL) {
        ESP_LOGE(BACKEND_TAG, "Fallo al crear transporte WS");
        ws_close();
        return ESP_FAIL;
    }

    char path[128];
    snprintf(path, sizeof(path), "/?key=%s", s_device_key);
    esp_transport_ws_set_path(s_ws, path);

    char headers[192];
    snprintf(headers, sizeof(headers), "key: %s\r\nOrigin: https://%s\r\n",
             s_device_key, BACKEND_HOST);
    esp_transport_ws_set_headers(s_ws, headers);
    esp_transport_ws_set_user_agent(s_ws, "Alarma/" BACKEND_FW_VERSION);

    int rc = esp_transport_connect(s_ws, BACKEND_HOST, BACKEND_PORT, BACKEND_CONNECT_TIMEOUT_MS);
    if (rc != 0) {
        ESP_LOGW(BACKEND_TAG, "Fallo de conexion TCP/TLS a %s:%d", BACKEND_HOST, BACKEND_PORT);
        ws_close();
        return ESP_FAIL;
    }

    int status = esp_transport_ws_get_upgrade_request_status(s_ws);
    if (status != 101) {
        ESP_LOGW(BACKEND_TAG, "Handshake WS rechazado, status=%d", status);
        ws_close();
        return ESP_FAIL;
    }

    s_intentos_ws = 0;
    s_ws_reconnect_ms = WS_RECONNECT_INITIAL_MS;
    s_connected = true;
    return ESP_OK;
}

static int ws_read_message(char* buf, int max_len) {
    int poll = esp_transport_poll_read(s_ws, BACKEND_POLL_TIMEOUT_MS);
    if (poll == 0) {
        return 0;
    }
    if (poll < 0) {
        return -1;
    }
    int len = esp_transport_read(s_ws, buf, max_len - 1, BACKEND_READ_TIMEOUT_MS);
    if (len < 0) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }
    buf[len] = '\0';
    if (esp_transport_ws_get_read_opcode(s_ws) != WS_TRANSPORT_OPCODES_TEXT) {
        return 0;
    }
    return len;
}

static void ws_handle_message(const char* msg, int len) {
    char evento[32];
    if (!json_get_string(msg, "event", evento, sizeof(evento))) {
        return;
    }

    if (strcmp(evento, "update_threshold") == 0 || strcmp(evento, "current_threshold") == 0) {
        const char* v = json_get_number_str(msg, "threshold");
        if (v != NULL) {
            s_umbral = strtof(v, NULL);
            sensor_set_threshold(s_umbral);
            nvs_save_config();
            ESP_LOGI(BACKEND_TAG, "NUEVO UMBRAL RECIBIDO: %.2f", (double) s_umbral);
        }
    } else if (strcmp(evento, "device_config_update") == 0) {
        const char* v = json_get_number_str(msg, "readIntervalMs");
        if (v != NULL) {
            s_intervalo_ms = (uint32_t) strtoul(v, NULL, 10);
            ESP_LOGI(BACKEND_TAG, "NUEVO INTERVALO: %lu ms", (unsigned long) s_intervalo_ms);
        }
        v = json_get_number_str(msg, "scaleFactor");
        if (v != NULL) {
            s_escala = strtof(v, NULL);
            sensor_set_scale(s_escala);
            ESP_LOGI(BACKEND_TAG, "NUEVA ESCALA: %.2f", (double) s_escala);
        }
        nvs_save_config();
        ws_send_text("{\"event\":\"config_ack\",\"data\":{\"ok\":true}}");
    } else if (strcmp(evento, "reading_ack") == 0) {
        ESP_LOGI(BACKEND_TAG, "Lectura confirmada por servidor");
    } else if (strcmp(evento, "auth_error") == 0) {
        ESP_LOGW(BACKEND_TAG, "AUTH_ERROR: key revocada o invalida. Marcando para reprovisionar...");
        s_credenciales_invalidadas = true;
        ws_close();
    }
}

static void backend_task(void* arg) {
    int64_t ahora = esp_timer_get_time() / 1000;
    s_ultima_lectura_ms = ahora;
    s_ultimo_device_info_ms = ahora;

    for (;;) {
        if (s_credenciales_invalidadas) {
            ESP_LOGW(BACKEND_TAG, "KEY INVALIDA CONFIRMADA - Limpiando credenciales y reiniciando...");
            network_clear_credentials();
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        }

        if (!s_connected) {
            if (ws_connect() != ESP_OK) {
                s_intentos_ws++;
                ESP_LOGW(BACKEND_TAG, "Fallo de conexion %d/%d. Reintento en %lu ms",
                         s_intentos_ws, MAX_INTENTOS_WS, s_ws_reconnect_ms);
                if (s_intentos_ws >= MAX_INTENTOS_WS) {
                    s_intentos_ws = 0;
                    diag_ap_start();
                }
                vTaskDelay(pdMS_TO_TICKS(s_ws_reconnect_ms));
                s_ws_reconnect_ms *= 2;
                if (s_ws_reconnect_ms > WS_RECONNECT_MAX_MS) {
                    s_ws_reconnect_ms = WS_RECONNECT_MAX_MS;
                }
                continue;
            }

            diag_ap_stop();
            ESP_LOGI(BACKEND_TAG, "WebSocket conectado a %s", BACKEND_HOST);

            char payload[128];
            snprintf(payload, sizeof(payload), "{\"event\":\"get_threshold\",\"data\":{\"blowerId\":\"\"}}");
            ws_send_text(payload);
            ws_send_device_info();

            ahora = esp_timer_get_time() / 1000;
            s_ultima_lectura_ms = ahora;
            s_ultimo_device_info_ms = ahora;
        }

        if (s_connected) {
            char buf[BACKEND_WS_READ_BUF];
            int len = ws_read_message(buf, sizeof(buf));
            if (len < 0) {
                ESP_LOGW(BACKEND_TAG, "Conexion WS perdida. Reconectando...");
                ws_close();
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            if (len > 0) {
                ws_handle_message(buf, len);
            }

            ahora = esp_timer_get_time() / 1000;

            bool alerta = sensor_get_alert();
            bool hubo_cambio = (alerta != s_alert_anterior);
            s_alert_anterior = alerta;

            if (ahora - s_ultima_lectura_ms >= (int64_t) s_intervalo_ms || hubo_cambio) {
                ws_send_pressure(ahora);
                s_ultima_lectura_ms = ahora;
            }

            if (ahora - s_ultimo_device_info_ms >= DEVICE_INFO_INTERVAL_MS) {
                ws_send_device_info();
                s_ultimo_device_info_ms = ahora;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BACKEND_LOOP_DELAY_MS));
    }
}

esp_err_t backend_init(void) {
    const char* key = network_get_device_key();
    if (key != NULL) {
        strncpy(s_device_key, key, sizeof(s_device_key) - 1);
        s_device_key[sizeof(s_device_key) - 1] = '\0';
    }

    nvs_load_config();
    sensor_set_threshold(s_umbral);
    sensor_set_scale(s_escala);
    s_alert_anterior = sensor_get_alert();

    ESP_LOGI(BACKEND_TAG, "Backend listo. deviceKey=%s intervalo=%lu ms escala=%.2f umbral=%.2f",
             s_device_key[0] != '\0' ? s_device_key : "(vacio)",
             (unsigned long) s_intervalo_ms, (double) s_escala, (double) s_umbral);
    return ESP_OK;
}

esp_err_t backend_start(void) {
    BaseType_t ret = xTaskCreate(backend_task, "BackendTask", BACKEND_TASK_STACK, NULL,
                                 BACKEND_TASK_PRIORITY, NULL);
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}
