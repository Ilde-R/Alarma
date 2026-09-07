#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hx711.h"

#define HX711_TAG "HX711"
#define HX711_READ_TIMEOUT_US 100000
#define HX711_PULSE_DELAY_US 1

esp_err_t hx711_init(hx711_t* dev, int dout_pin, int sck_pin) {
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    dev->dout_pin = dout_pin;
    dev->sck_pin = sck_pin;
    dev->offset = 0;
    dev->scale = 1.0f;

    gpio_config_t dout_cfg = {
        .pin_bit_mask = (1ULL << dout_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&dout_cfg);

    gpio_config_t sck_cfg = {
        .pin_bit_mask = (1ULL << sck_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&sck_cfg);
    gpio_set_level(sck_pin, 0);

    return ESP_OK;
}

bool hx711_is_ready(hx711_t* dev) {
    if (dev == NULL) {
        return false;
    }
    return gpio_get_level(dev->dout_pin) == 0;
}

int32_t hx711_read_raw(hx711_t* dev) {
    if (dev == NULL) {
        return 0;
    }

    int64_t start = esp_timer_get_time();
    while (gpio_get_level(dev->dout_pin) != 0) {
        if (esp_timer_get_time() - start > HX711_READ_TIMEOUT_US) {
            ESP_LOGW(HX711_TAG, "Timeout esperando DOUT");
            return 0;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    int32_t value = 0;
    for (int i = 0; i < 24; i++) {
        gpio_set_level(dev->sck_pin, 1);
        esp_rom_delay_us(HX711_PULSE_DELAY_US);
        value <<= 1;
        if (gpio_get_level(dev->dout_pin)) {
            value |= 1;
        }
        gpio_set_level(dev->sck_pin, 0);
        esp_rom_delay_us(HX711_PULSE_DELAY_US);
    }

    gpio_set_level(dev->sck_pin, 1);
    esp_rom_delay_us(HX711_PULSE_DELAY_US);
    gpio_set_level(dev->sck_pin, 0);

    if (value & 0x800000) {
        value |= ~0xFFFFFF;
    }

    return value;
}

float hx711_get_value(hx711_t* dev) {
    if (dev == NULL) {
        return 0.0f;
    }
    return (float)(hx711_read_raw(dev) - dev->offset);
}

float hx711_get_units(hx711_t* dev) {
    if (dev == NULL) {
        return 0.0f;
    }
    if (dev->scale == 0.0f) {
        return 0.0f;
    }
    return hx711_get_value(dev) / dev->scale;
}

void hx711_tare(hx711_t* dev, uint8_t times) {
    if (dev == NULL || times == 0) {
        return;
    }
    int32_t sum = 0;
    uint8_t count = 0;
    for (uint8_t i = 0; i < times; i++) {
        int32_t raw = hx711_read_raw(dev);
        if (raw == 0) {
            continue;
        }
        sum += raw;
        count++;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (count > 0) {
        dev->offset = sum / count;
    }
}

void hx711_set_scale(hx711_t* dev, float scale) {
    if (dev != NULL) {
        dev->scale = scale;
    }
}

void hx711_set_offset(hx711_t* dev, int32_t offset) {
    if (dev != NULL) {
        dev->offset = offset;
    }
}
