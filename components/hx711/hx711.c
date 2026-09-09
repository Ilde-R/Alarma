#include "hx711.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

#define HX711_TAG "HX711"
#define HX711_READ_TIMEOUT_US    200000
#define HX711_PULSE_DELAY_US     1



esp_err_t hx711_init(hx711_t *dev, int dout_pin, int sck_pin)
{
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    dev->dout_pin = dout_pin;
    dev->sck_pin = sck_pin;

    dev->offset = 0;
    dev->scale = 1.0f;


    /*
     * DOUT
     */
    gpio_config_t dout_cfg = {
        .pin_bit_mask = (1ULL << dout_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&dout_cfg);

    if (err != ESP_OK) {
        ESP_LOGE(
            HX711_TAG,
            "Error configurando DOUT GPIO%d: %s",
            dout_pin,
            esp_err_to_name(err)
        );

        return err;
    }


    /*
     * SCK
     */
    gpio_config_t sck_cfg = {
        .pin_bit_mask = (1ULL << sck_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    err = gpio_config(&sck_cfg);

    if (err != ESP_OK) {
        ESP_LOGE(
            HX711_TAG,
            "Error configurando SCK GPIO%d: %s",
            sck_pin,
            esp_err_to_name(err)
        );

        return err;
    }
    gpio_set_level(sck_pin, 0);


    ESP_LOGI(
        HX711_TAG,
        "HX711 inicializado DOUT=GPIO%d SCK=GPIO%d",
        dout_pin,
        sck_pin
    );

    return ESP_OK;
}


/*
 * DOUT LOW significa que el HX711 tiene una conversión disponible.
 */
bool hx711_is_ready(hx711_t *dev)
{
    if (dev == NULL) {
        return false;
    }

    return gpio_get_level(dev->dout_pin) == 0;
}


/*
 * Lee exactamente 24 bits.
 *
 * Después de los 24 bits se genera un pulso adicional:
 *
 * 1 pulso  -> canal A, ganancia 128
 *
 * Esto reproduce el comportamiento normal de la librería
 * HX711 utilizada por Arduino.
 */
esp_err_t hx711_read_raw(hx711_t *dev, int32_t *value)
{
    if (dev == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }


    /*
     * Esperar hasta que DOUT pase a LOW.
     */
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(dev->dout_pin) != 0) {
        if ((esp_timer_get_time() - start) >= HX711_READ_TIMEOUT_US) {
            ESP_LOGW(HX711_TAG, "Timeout esperando DOUT del HX711");
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    uint32_t raw = 0;

    portENTER_CRITICAL(&mux);

    for (int i = 0; i < 24; i++) {
        // SCK HIGH
        gpio_set_level(dev->sck_pin, 1);
        esp_rom_delay_us(HX711_PULSE_DELAY_US);

        // Leer bit
        raw <<= 1;
        if (gpio_get_level(dev->dout_pin)) {
            raw |= 1;
        }

        // SCK LOW
        gpio_set_level(dev->sck_pin, 0);
        esp_rom_delay_us(HX711_PULSE_DELAY_US);
    }


    /*
     * Pulsos 25, 26 y 27:
     * Canal A
     * Ganancia 64
     */
    for(int p = 0; p < 3; p++) {
        gpio_set_level(dev->sck_pin, 1);
        esp_rom_delay_us(HX711_PULSE_DELAY_US);
        gpio_set_level(dev->sck_pin, 0);
        esp_rom_delay_us(HX711_PULSE_DELAY_US);
    }
    portEXIT_CRITICAL(&mux);

    int32_t signed_value;
    if (raw & 0x800000) {
        signed_value = (int32_t)(raw | 0xFF000000);
    } else {
        signed_value = (int32_t)raw;
    }

    *value = signed_value;
    return ESP_OK;
}


/*
 * Valor bruto después de restar tare.
 */
esp_err_t hx711_get_value(hx711_t *dev, float *value)
{
    if (dev == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (dev->scale == 0.0f) {
        return ESP_ERR_INVALID_STATE;
    }


    int32_t raw;

    esp_err_t err = hx711_read_raw(dev, &raw);

    if (err != ESP_OK) {
        return err;
    }


    *value = (float)(raw - dev->offset);

    return ESP_OK;
}


/*
 * Valor calibrado.
 */
esp_err_t hx711_get_units(hx711_t *dev, float *units)
{
    if (dev == NULL || units == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (dev->scale == 0.0f) {
        return ESP_ERR_INVALID_STATE;
    }

    int32_t raw;

    esp_err_t err = hx711_read_raw(dev, &raw);

    if (err != ESP_OK) {
        return err;
    }

    float value = (float)(raw - dev->offset);

    *units = value / dev->scale;

    // ESP_LOGI(
    //     HX711_TAG,
    //     "RAW=%ld | OFFSET=%ld | VALUE=%.0f | SCALE=%.2f | UNITS=%.2f",
    //     (long)raw,
    //     (long)dev->offset,
    //     (double)value,
    //     (double)dev->scale,
    //     (double)*units
    // );

    return ESP_OK;
}


/*
 * Tare.
 *
 *
 * sensorPresion.tare(20);
 *
 * Se toman varias lecturas y se calcula el promedio.
 */
esp_err_t hx711_tare(hx711_t *dev, uint8_t times)
{
    if (dev == NULL || times == 0) {
        return ESP_ERR_INVALID_ARG;
    }


    int64_t sum = 0;
    uint8_t count = 0;


    for (uint8_t i = 0; i < times; i++) {

        int32_t raw;

        esp_err_t err = hx711_read_raw(dev, &raw);

        if (err != ESP_OK) {

            ESP_LOGW(
                HX711_TAG,
                "Error durante tare %u/%u: %s",
                i + 1,
                times,
                esp_err_to_name(err)
            );

            continue;
        }


        sum += raw;
        count++;


        /*
         * Pequeña pausa entre muestras.
         */
        vTaskDelay(pdMS_TO_TICKS(10));
    }


    if (count == 0) {

        ESP_LOGE(
            HX711_TAG,
            "No se pudieron obtener lecturas durante tare"
        );

        return ESP_ERR_TIMEOUT;
    }


    dev->offset = (int32_t)(sum / count);


    ESP_LOGI(
        HX711_TAG,
        "Tare completado. Offset=%ld usando %u muestras",
        (long)dev->offset,
        count
    );


    return ESP_OK;
}


void hx711_set_scale(hx711_t *dev, float scale)
{
    if (dev == NULL) {
        return;
    }

    dev->scale = scale;
}


void hx711_set_offset(hx711_t *dev, int32_t offset)
{
    if (dev == NULL) {
        return;
    }

    dev->offset = offset;
}