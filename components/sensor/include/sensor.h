#ifndef SENSOR_H
#define SENSOR_H

#include <stdbool.h>
#include "esp_err.h"

#define SENSOR_DOUT_PIN     3
#define SENSOR_SCK_PIN      4
#define SENSOR_OUTPUT_PIN   5

#define SENSOR_DEFAULT_SCALE    25000.0f

#define SENSOR_DEFAULT_UMBRAL   10.0f

#define MAX_RANGE_PSI            330.0f
#define SUSPICIOUS_JUMP_PSI     60.0f
#define REQUIRED_CONFIRMATIONS   2

esp_err_t sensor_init(void);
esp_err_t sensor_start(void);

float sensor_get_pressure(void);
float sensor_get_threshold(void);

void sensor_set_threshold(float psi);
void sensor_set_scale(float escala);

bool sensor_get_alert(void);

#endif