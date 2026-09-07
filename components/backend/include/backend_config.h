#ifndef BACKEND_CONFIG_H
#define BACKEND_CONFIG_H

#include <stdint.h>
#include "esp_err.h"

typedef struct {
    float threshold;
    float scale;
    uint32_t interval_ms;
} backend_config_t;

void backend_config_set_defaults(backend_config_t* config);
esp_err_t backend_config_load(backend_config_t* config);
esp_err_t backend_config_save(const backend_config_t* config);

#endif
