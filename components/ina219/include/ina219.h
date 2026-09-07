#ifndef INA219_H
#define INA219_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    float current_ma;
    float bus_voltage_v;
    float power_mw;
} ina219_reading_t;

esp_err_t ina219_init(int sda_pin, int scl_pin, uint8_t address);
esp_err_t ina219_read(ina219_reading_t* reading);
bool ina219_is_ready(void);

#endif