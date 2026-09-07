#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network.h"
#include "provision.h"
#include "sensor.h"
#include "backend.h"
#include "backend_config.h"
#include "backend_protocol.h"
#include "backend_ws.h"

#define BACKEND_TAG "BACKEND"
#define BACKEND_WS_READ_BUF 512
#define DEVICE_INFO_INTERVAL_MS 300000
#define WS_RECONNECT_INITIAL_MS 1000
#define WS_RECONNECT_MAX_MS 30000
#define BACKEND_TASK_STACK 8192
#define BACKEND_TASK_PRIORITY 2
#define BACKEND_LOOP_DELAY_MS 50

static backend_ws_t s_ws;
static backend_config_t s_config;
static bool s_credentials_invalid = false;
static bool s_reprovisioning = false;
static unsigned long s_ws_reconnect_ms = WS_RECONNECT_INITIAL_MS;
static int64_t s_last_reading_ms = 0;
static int64_t s_last_device_info_ms = 0;
static bool s_previous_alert = false;
static char s_device_key[NETWORK_DEVICE_KEY_MAX_LEN] = "";

static void backend_task(void* arg) {
    (void) arg;
    int64_t now = esp_timer_get_time() / 1000;
    s_last_reading_ms = now;
    s_last_device_info_ms = now;

    for (;;) {
        if (s_credentials_invalid) {
            ESP_LOGW(BACKEND_TAG, "KEY INVALIDA - Limpiando credenciales y reiniciando...");
            network_clear_credentials();
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        }

        if (s_reprovisioning) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (!backend_ws_is_connected(&s_ws)) {
            if (backend_ws_connect(&s_ws, s_device_key) != ESP_OK) {
                ESP_LOGW(BACKEND_TAG, "Fallo de conexion. Reintento en %lu ms", s_ws_reconnect_ms);
                vTaskDelay(pdMS_TO_TICKS(s_ws_reconnect_ms));
                s_ws_reconnect_ms *= 2;
                if (s_ws_reconnect_ms > WS_RECONNECT_MAX_MS) {
                    s_ws_reconnect_ms = WS_RECONNECT_MAX_MS;
                }
                continue;
            }

            ESP_LOGI(BACKEND_TAG, "WebSocket conectado");
            backend_ws_send_text(&s_ws, "{\"event\":\"get_threshold\",\"data\":{\"blowerId\":\"\"}}");
            backend_protocol_send_device_info(&s_ws, s_device_key);
            now = esp_timer_get_time() / 1000;
            s_last_reading_ms = now;
            s_last_device_info_ms = now;
        }

        if (backend_ws_is_connected(&s_ws)) {
            char buffer[BACKEND_WS_READ_BUF];
            int length = backend_ws_read_message(&s_ws, buffer, sizeof(buffer));
            if (length == -2) {
                ESP_LOGW(BACKEND_TAG, "Servidor cerro WS. Abriendo provisionamiento...");
                backend_ws_close(&s_ws);
                provision_start();
                s_reprovisioning = true;
                continue;
            }
            if (length < 0) {
                ESP_LOGW(BACKEND_TAG, "Conexion WS perdida. Reconectando...");
                backend_ws_close(&s_ws);
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            if (length > 0) {
                backend_protocol_handle_message(&s_ws, buffer, &s_config, &s_credentials_invalid);
            }

            now = esp_timer_get_time() / 1000;
            bool alert = sensor_get_alert();
            bool alert_changed = alert != s_previous_alert;
            s_previous_alert = alert;

            if (now - s_last_reading_ms >= (int64_t) s_config.interval_ms || alert_changed) {
                backend_protocol_send_pressure(&s_ws, now);
                s_last_reading_ms = now;
            }
            if (now - s_last_device_info_ms >= DEVICE_INFO_INTERVAL_MS) {
                backend_protocol_send_device_info(&s_ws, s_device_key);
                s_last_device_info_ms = now;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BACKEND_LOOP_DELAY_MS));
    }
}

esp_err_t backend_init(void) {
    backend_ws_init(&s_ws);

    const char* device_key = network_get_device_key();
    if (device_key != NULL) {
        strncpy(s_device_key, device_key, sizeof(s_device_key) - 1);
        s_device_key[sizeof(s_device_key) - 1] = '\0';
    }

    esp_err_t config_err = backend_config_load(&s_config);
    if (config_err != ESP_OK) {
        ESP_LOGW(BACKEND_TAG, "No se pudo cargar configuracion: %s", esp_err_to_name(config_err));
    }
    sensor_set_threshold(s_config.threshold);
    sensor_set_scale(s_config.scale);
    s_previous_alert = sensor_get_alert();

    ESP_LOGI(BACKEND_TAG, "Backend listo. intervalo=%lu ms escala=%.2f umbral=%.2f",
             (unsigned long) s_config.interval_ms,
             (double) s_config.scale,
             (double) s_config.threshold);
    return ESP_OK;
}

esp_err_t backend_start(void) {
    BaseType_t result = xTaskCreate(backend_task, "BackendTask", BACKEND_TASK_STACK, NULL,
                                    BACKEND_TASK_PRIORITY, NULL);
    return result == pdPASS ? ESP_OK : ESP_FAIL;
}
