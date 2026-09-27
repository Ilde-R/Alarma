#ifndef SENSOR_H
#define SENSOR_H

#include <stdbool.h>
#include "esp_err.h"

#define SENSOR_OUTPUT_PIN   5

#define SENSOR_OFFSET_MV 595.0f
#define SENSOR_FULL_SCALE_MV 3000.0f
#define SENSOR_MAX_KPA 40.0f

#define KPA_TO_PSI 0.145038f
#define SENSOR_DEFAULT_SCALE 0.8095f
#define SENSOR_MAX_PSI 5.80f

#define SENSOR_DEFAULT_UMBRAL 1.20f
#define SUSPICIOUS_JUMP_PSI   4.00f 
#define REQUIRED_CONFIRMATIONS 2

esp_err_t sensor_init(void);
esp_err_t sensor_start(void);

float sensor_get_pressure(void);
float sensor_get_threshold(void);

void sensor_set_threshold(float psi);

bool sensor_get_alert(void);

void sensor_set_scale(float scale);

#endif 