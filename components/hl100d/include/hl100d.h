#ifndef HL100D_H
#define HL100D_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"

typedef struct {
    float pressure_kpa;
    float voltage_mv;
    uint32_t raw_adc;
} hl100d_reading_t;

typedef struct {
    adc_unit_t adc_unit;
    adc_channel_t adc_channel;
    float offset_mv;
    float full_scale_mv;
    float max_pressure_kpa;
} hl100d_config_t;

typedef struct {
    adc_oneshot_unit_handle_t adc_handle;
    hl100d_config_t config;
} hl100d_t;

esp_err_t hl100d_init(hl100d_t*sensor, const hl100d_config_t*config);
esp_err_t hl100d_read(hl100d_t*sensor, hl100d_reading_t*reading);


#endif