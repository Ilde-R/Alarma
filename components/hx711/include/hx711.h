#ifndef HX711_H
#define HX711_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    int dout_pin;
    int sck_pin;

    int32_t offset;
    float scale;
} hx711_t;


/*
 * Inicialización
 */
esp_err_t hx711_init(hx711_t *dev, int dout_pin, int sck_pin);


/*
 * Estado del HX711
 *
 * true  = hay una conversión disponible
 * false = todavía está convirtiendo
 */
bool hx711_is_ready(hx711_t *dev);


/*
 * Lectura cruda de 24 bits.
 *
 * Devuelve ESP_OK mediante el argumento error si la lectura
 * fue válida.
 */
esp_err_t hx711_read_raw(hx711_t *dev, int32_t *value);


/*
 * Obtiene el valor bruto sin aplicar escala.
 */
esp_err_t hx711_get_value(hx711_t *dev, float *value);


/*
 * Obtiene unidades calibradas.
 */
esp_err_t hx711_get_units(hx711_t *dev, float *units);


/*
 * Tara.
 */
esp_err_t hx711_tare(hx711_t *dev, uint8_t times);


/*
 * Configuración.
 */
void hx711_set_scale(hx711_t *dev, float scale);
void hx711_set_offset(hx711_t *dev, int32_t offset);

#endif