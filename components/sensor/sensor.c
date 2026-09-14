#include "sensor.h"
#include "hl100d.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <math.h>

#define SENSOR_ADC_UNIT ADC_UNIT_1
#define SENSOR_ADC_CHANNEL ADC_CHANNEL_0

static const char *TAG = "SENSOR_LOGIC";

static hl100d_t pressure_sensor;
static float current_pressure = 0.0f;  
static float last_valid_pressure = 0.0f;
static float current_threshold = SENSOR_DEFAULT_UMBRAL;
static bool alert_active = false;
static int confirmation_counter = 0;

esp_err_t sensor_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SENSOR_OUTPUT_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(SENSOR_OUTPUT_PIN, 0); 

    hl100d_config_t hl_config = {
        .adc_unit = SENSOR_ADC_UNIT,
        .adc_channel = SENSOR_ADC_CHANNEL,
        .offset_mv = SENSOR_OFFSET_MV,
        .full_scale_mv = SENSOR_FULL_SCALE_MV,
        .max_pressure_kpa = SENSOR_MAX_KPA
    };

    return hl100d_init(&pressure_sensor, &hl_config);
}

static void sensor_task(void *pvParameters) {
    hl100d_reading_t reading;
    
    while (1) {
        if (hl100d_read(&pressure_sensor, &reading) == ESP_OK) {
            float new_pressure = reading.pressure_kpa * KPA_TO_PSI;

            if (new_pressure <= SENSOR_MAX_PSI) {
                
                if (fabs(new_pressure - last_valid_pressure) > SUSPICIOUS_JUMP_PSI) {
                    ESP_LOGW(TAG, "Salto de presion! Anterior: %.2f PSI, Nuevo: %.2f PSI", last_valid_pressure, new_pressure);
                } else {
                    current_pressure = new_pressure;
                    last_valid_pressure = new_pressure;
                    
                    if (current_pressure < current_threshold) {
                        confirmation_counter++;
                        if (confirmation_counter >= REQUIRED_CONFIRMATIONS) {
                            alert_active = true;
                            gpio_set_level(SENSOR_OUTPUT_PIN, 1);
                            ESP_LOGE(TAG, "ALARMA ACTIVA! Actual: %.2f PSI", current_pressure);
                        }
                    } else {
                        confirmation_counter = 0;
                        if (alert_active) {
                            alert_active = false;
                            gpio_set_level(SENSOR_OUTPUT_PIN, 0); 
                            ESP_LOGI(TAG, "Presion normalizada. Alarma desactivada.");
                        } else {
                            ESP_LOGI(TAG, "Presion normal. Actual: %.2f PSI", current_pressure);
                        }
                    }
                }
            }
        } else {
            ESP_LOGE(TAG, "Failed to read sensor hardware");
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t sensor_start(void) {
    BaseType_t res = xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
    return (res == pdPASS) ? ESP_OK : ESP_FAIL;
}

float sensor_get_pressure(void) {
    return current_pressure;
}

float sensor_get_threshold(void) {
    return current_threshold;
}

void sensor_set_threshold(float psi) {
    current_threshold = psi;
    ESP_LOGI(TAG, "Threshold updated to: %.2f PSI", current_threshold);
}

bool sensor_get_alert(void) {
    return alert_active;
}