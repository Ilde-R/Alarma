#ifndef LED_H
#define LED_H

#include "esp_err.h"
#include <stdbool.h>

#define LED_ONBOARD_PIN 8

esp_err_t led_init(void);

esp_err_t led_set_alert(bool alert);

#endif