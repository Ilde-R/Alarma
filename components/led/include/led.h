#ifndef LED_H
#define LED_H

#include "esp_err.h"

#define LED_WS2812_PIN 8

esp_err_t led_init(void);

esp_err_t led_set_color(uint8_t red, uint8_t green, uint8_t blue);

esp_err_t led_set_alert(bool alert);

#endif
