#ifndef XGZP6847A_H
#define XGZP6847A_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"

typedef struct {
    float pressure_kpa;
    float voltage_mv;
    uint32_t raw_adc;
} xgzp6847a_reading_t;

typedef struct {
    adc_unit_t adc_unit;
    adc_channel_t adc_channel;
    float offset_mv;
    float full_scale_mv;
    float max_pressure_kpa;
} xgzp6847a_config_t;

typedef struct {
    adc_oneshot_unit_handle_t adc_handle;
    xgzp6847a_config_t config;
} xgzp6847a_t;

esp_err_t xgzp6847a_init(xgzp6847a_t*sensor, const xgzp6847a_config_t*config);
esp_err_t xgzp6847a_read(xgzp6847a_t*sensor, xgzp6847a_reading_t*reading);


#endif