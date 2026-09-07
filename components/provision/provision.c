#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "network.h"
#include "provision.h"

#define PROVISION_MAX_BODY 512
#define PROVISION_HTTPD_MAX_OPEN_SOCKETS 4
#define PROVISION_HTTPD_STACK 4096

static const char* TAG = "PROVISION";

static httpd_handle_t s_server = NULL;

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
                            char c;
                            switch (*q) {
                                case 'n': c = '\n'; break;
                                case 'r': c = '\r'; break;
                                case 't': c = '\t'; break;
                                case 'b': c = '\b'; break;
                                case 'f': c = '\f'; break;
                                case 'u': q += 4; c = '?'; break;
                                default:  c = *q; break;
                            }
                            if (i + 1 < out_len) out[i++] = c;
                            q++;
                            continue;
                        }
                        if (i + 1 < out_len) out[i++] = *q;
                        q++;
                    }
                    if (*q == '"') {
                        if (out_len > 0) {
                            out[i < out_len ? i : out_len - 1] = '\0';
                        }
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

static esp_err_t configure_handler(httpd_req_t* req) {
    char body[PROVISION_MAX_BODY + 1];
    char ssid[NETWORK_SSID_MAX_LEN];
    char pass[NETWORK_PASS_MAX_LEN] = "";
    char device_key[NETWORK_DEVICE_KEY_MAX_LEN];
    char device_name[NETWORK_DEVICE_NAME_MAX_LEN] = "";

    int total = req->content_len;
    if (total <= 0 || total > PROVISION_MAX_BODY) {
        httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "Cuerpo demasiado grande");
        return ESP_OK;
    }
    int received = 0;
    while (received < total) {
        int chunk = httpd_req_recv(req, body + received, total - received);
        if (chunk <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Cuerpo incompleto");
            return ESP_OK;
        }
        received += chunk;
    }
    body[received] = '\0';

    if (!json_get_string(body, "ssid", ssid, sizeof(ssid)) ||
        !json_get_string(body, "deviceKey", device_key, sizeof(device_key))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Faltan ssid y/o deviceKey");
        return ESP_OK;
    }

    json_get_string(body, "pass", pass, sizeof(pass));
    json_get_string(body, "name", device_name, sizeof(device_name));

    esp_err_t err = network_save_credentials(ssid, pass, device_key, device_name);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error al guardar");
        return ESP_OK;
    }

    httpd_resp_sendstr(req, "OK. Guardado. Reiniciando...");
    ESP_LOGI(TAG, "Credenciales guardadas. Reiniciando en 1s...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

esp_err_t provision_http_start(void) {
    if (s_server != NULL) {
        return ESP_OK;
    }

    httpd_config_t httpd_cfg = HTTPD_DEFAULT_CONFIG();
    httpd_cfg.max_open_sockets = PROVISION_HTTPD_MAX_OPEN_SOCKETS;
    httpd_cfg.stack_size = PROVISION_HTTPD_STACK;
    esp_err_t err = httpd_start(&s_server, &httpd_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al iniciar servidor HTTP: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t uri = {
        .uri = "/configure",
        .method = HTTP_POST,
        .handler = configure_handler,
    };
    err = httpd_register_uri_handler(s_server, &uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al registrar /configure: %s", esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    ESP_LOGI(TAG, "Servidor HTTP activo en 192.168.4.1: POST /configure");
    return ESP_OK;
}

void provision_http_stop(void) {
    if (s_server != NULL) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "Servidor HTTP detenido.");
    }
}

esp_err_t provision_start(void) {
    esp_err_t err = network_ap_start(WIFI_MODE_AP);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al iniciar AP de provisionamiento: %s", esp_err_to_name(err));
        return err;
    }

    err = provision_http_start();
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Modo provisionamiento activo: WiFi '%s' (abierta) | IP 192.168.4.1 | POST /configure",
             NETWORK_AP_SSID);
    return ESP_OK;
}
