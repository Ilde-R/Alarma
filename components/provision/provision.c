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

static void delayed_restart_task(void* arg) {
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
    vTaskDelete(NULL); 
}

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
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

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

    httpd_resp_sendstr(req, "OK");
    ESP_LOGI(TAG, "Credenciales guardadas. Reiniciando en 1.5s...");

    xTaskCreate(delayed_restart_task, "RestartTask", 2048, NULL, 5, NULL);

    return ESP_OK;
}

static esp_err_t options_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    httpd_resp_send(req, NULL, 0); 
    return ESP_OK;
}

static esp_err_t html_handler(httpd_req_t *req) {
    const char* html_page = 
        "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<style>body{font-family:sans-serif; padding:20px; background:#f4f4f9;} "
        "input, button{width:100%; padding:10px; margin:8px 0; border-radius:5px; border:1px solid #ccc; box-sizing:border-box;}"
        "button{background:#007BFF; color:white; font-weight:bold; border:none; padding:15px; margin-top:15px;}</style></head>"
        "<body><h2>Configurar Sensor</h2>"
        "<input type='text' id='s' placeholder='Nombre del WiFi (SSID)'>"
        "<input type='password' id='p' placeholder='Contrase&ntilde;a del WiFi'>"
        "<input type='text' id='k' placeholder='Device Key'>"
        "<input type='text' id='n' placeholder='Nombre del Equipo'>"
        "<button onclick='enviar()'>Guardar y Conectar</button>"
        "<script>function enviar(){"
        "var d={ssid:document.getElementById('s').value, pass:document.getElementById('p').value,"
        "deviceKey:document.getElementById('k').value, name:document.getElementById('n').value};"
        "fetch('/configure',{method:'POST',body:JSON.stringify(d)}).then(r=>alert('Listo! El equipo se reiniciara. Ya puedes cerrar esta pagina.'));"
        "}</script></body></html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_page, strlen(html_page));
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

    httpd_uri_t uri_get = { .uri = "/", .method = HTTP_GET, .handler = html_handler };
    httpd_register_uri_handler(s_server, &uri_get);

    httpd_uri_t uri_options = { .uri = "/configure", .method = HTTP_OPTIONS, .handler = options_handler };
    httpd_register_uri_handler(s_server, &uri_options);

    httpd_uri_t uri_post = { .uri = "/configure", .method = HTTP_POST, .handler = configure_handler };
    httpd_register_uri_handler(s_server, &uri_post);

    ESP_LOGI(TAG, "Servidor HTTP activo en 192.168.4.1 (Web y POST)");
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

    ESP_LOGI(TAG, "Modo provisionamiento activo: WiFi '%s' (abierta) | IP 192.168.4.1 | Web/POST",
             NETWORK_AP_SSID);
    return ESP_OK;
}