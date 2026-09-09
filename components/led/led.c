#include "driver/gpio.h"
#include "esp_log.h"
#include "led.h"

#define LED_TAG "LED"

esp_err_t led_init(void) {
    gpio_reset_pin(LED_ONBOARD_PIN);
    gpio_set_direction(LED_ONBOARD_PIN, GPIO_MODE_OUTPUT);

    gpio_set_level(LED_ONBOARD_PIN, 0);

    ESP_LOGI(LED_TAG, "Led WS2812 configurado en GPIO%d", LED_ONBOARD_PIN);
    return ESP_OK;
}

esp_err_t led_set_alert(bool alert) {
    esp_err_t err = gpio_set_level(LED_ONBOARD_PIN, alert ? 0 : 1);
    return err;
}
