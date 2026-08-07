#include <math.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hx711.h"
#include "sensor.h"

#define SENSOR_TAG "SENSOR"

#define SENSOR_TASK_STACK 4096
#define SENSOR_TASK_PRIORITY 1
#define SENSOR_LOOP_DELAY_MS 100

#define RANGO_MAXIMO_PSI 330.0f
#define SALTO_SOSPECHOSO_PSI 60.0f
#define CONFIRMACIONES_REQUERIDAS 2

static hx711_t s_hx711;
static volatile float s_pressure = 0.0f;
static volatile float s_threshold = SENSOR_DEFAULT_UMBRAL;
static volatile bool s_alert = false;

static float mediana3(float a, float b, float c) {
    float tmp;
    if (a > b) { tmp = a; a = b; b = tmp; }
    if (b > c) { tmp = b; b = c; c = tmp; }
    if (a > b) { tmp = a; a = b; b = tmp; }
    return b;
}

static void sensor_task(void* arg) {
    float presion_anterior = 0.0f;
    uint8_t saltos_seguidos = 0;

    for (;;) {
        if (hx711_is_ready(&s_hx711)) {
            float p1 = hx711_get_units(&s_hx711);
            float p2 = hx711_get_units(&s_hx711);
            float p3 = hx711_get_units(&s_hx711);
            float psi = mediana3(p1, p2, p3);

            if (psi < 0.0f) {
                psi = 0.0f;
            }

            if (psi > RANGO_MAXIMO_PSI) {
                ESP_LOGW(SENSOR_TAG, "Lectura fuera de rango fisico, descartada: %.2f", psi);
                vTaskDelay(pdMS_TO_TICKS(SENSOR_LOOP_DELAY_MS));
                continue;
            }

            if (fabs(psi - presion_anterior) > SALTO_SOSPECHOSO_PSI) {
                saltos_seguidos++;
                if (saltos_seguidos < CONFIRMACIONES_REQUERIDAS) {
                    ESP_LOGW(SENSOR_TAG, "Posible glitch electrico, descartada: %.2f", psi);
                    vTaskDelay(pdMS_TO_TICKS(SENSOR_LOOP_DELAY_MS));
                    continue;
                }
            } else {
                saltos_seguidos = 0;
            }

            presion_anterior = psi;

            bool alerta = (psi <= s_threshold);
            s_pressure = psi;
            s_alert = alerta;

            gpio_set_level(SENSOR_OUTPUT_PIN, alerta ? 1 : 0);
            gpio_set_level(SENSOR_LED_PIN, alerta ? 0 : 1);
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_LOOP_DELAY_MS));
    }
}

esp_err_t sensor_init(void) {
    s_threshold = SENSOR_DEFAULT_UMBRAL;

    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << SENSOR_OUTPUT_PIN) | (1ULL << SENSOR_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&out_cfg);
    gpio_set_level(SENSOR_OUTPUT_PIN, 0);
    gpio_set_level(SENSOR_LED_PIN, 1);

    esp_err_t err = hx711_init(&s_hx711, SENSOR_DOUT_PIN, SENSOR_SCK_PIN);
    if (err != ESP_OK) {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));

    if (hx711_is_ready(&s_hx711)) {
        hx711_tare(&s_hx711, 20);
        hx711_set_scale(&s_hx711, SENSOR_DEFAULT_SCALE);
        ESP_LOGI(SENSOR_TAG, "Sensor calibrado. Monitoreo critico iniciado.");
    } else {
        ESP_LOGE(SENSOR_TAG, "ERROR: Sensor desconectado.");
    }

    return ESP_OK;
}

esp_err_t sensor_start(void) {
    BaseType_t ret = xTaskCreate(sensor_task, "SensorTask", SENSOR_TASK_STACK, NULL,
                                 SENSOR_TASK_PRIORITY, NULL);
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}

float sensor_get_pressure(void) {
    return s_pressure;
}

float sensor_get_threshold(void) {
    return s_threshold;
}

void sensor_set_threshold(float psi) {
    s_threshold = psi;
}

bool sensor_get_alert(void) {
    return s_alert;
}
