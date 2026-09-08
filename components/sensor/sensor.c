#include <math.h>

#include "driver/gpio.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hx711.h"
#include "led.h"
#include "sensor.h"


#define SENSOR_TAG "SENSOR"

#define SENSOR_TASK_STACK       4096
#define SENSOR_TASK_PRIORITY    5
#define SENSOR_LOOP_DELAY_MS    100
#define SENSOR_LOG_INTERVAL_MS  1000


static hx711_t s_hx711;
static volatile float s_pressure = 0.0f;
static volatile float s_threshold =
    SENSOR_DEFAULT_UMBRAL;
static volatile bool s_alert = false;


/*
 * Última presión válida.
 */
static float previous_pressure = 0.0f;


/*
 * Número de saltos sospechosos consecutivos.
 */
static uint8_t consecutive_jumps = 0;


/*
 * Tiempo del último log.
 */
static TickType_t last_log_time;


/*
 * Mediana de tres valores.
 */
static float median3(float a, float b, float c)
{
    float tmp;


    if (a > b) {
        tmp = a;
        a = b;
        b = tmp;
    }


    if (b > c) {
        tmp = b;
        b = c;
        c = tmp;
    }


    if (a > b) {
        tmp = a;
        a = b;
        b = tmp;
    }


    return b;
}


/*
 * Lee 3 muestras independientes
 */
static esp_err_t sensor_read_median(float *result)
{
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }


    float readings[3];


    for (int i = 0; i < 3; i++) {

        esp_err_t err =
            hx711_get_units(
                &s_hx711,
                &readings[i]
            );


        if (err != ESP_OK) {

            ESP_LOGW(
                SENSOR_TAG,
                "Lectura %d/3 del HX711 invalida: %s",
                i + 1,
                esp_err_to_name(err)
            );

            return err;
        }
    }


    *result = median3(
        readings[0],
        readings[1],
        readings[2]
    );


    return ESP_OK;
}


/*
 * Tarea principal del sensor.
 */
static void sensor_task(void *arg)
{
    (void)arg;


    ESP_LOGI(
        SENSOR_TAG,
        "Esperando 1 segundo para estabilizacion..."
    );

    vTaskDelay(pdMS_TO_TICKS(1000));


    /*
     * Comprobar HX711.
     */
    if (!hx711_is_ready(&s_hx711)) {

        ESP_LOGW(
            SENSOR_TAG,
            "HX711 no estaba listo al iniciar"
        );
    }


    /*
     * TARE.
     *
     * Arduino:
     *
     * sensorPresion.tare(20);
     */
    esp_err_t err =
        hx711_tare(
            &s_hx711,
            20
        );


    if (err != ESP_OK) {

        ESP_LOGE(
            SENSOR_TAG,
            "No se pudo hacer tare: %s",
            esp_err_to_name(err)
        );

    } else {
        hx711_set_scale(
            &s_hx711,
            SENSOR_DEFAULT_SCALE
        );


        ESP_LOGI(
            SENSOR_TAG,
            "Sensor calibrado."
        );

        ESP_LOGI(
            SENSOR_TAG,
            "Scale = %.2f",
            (double)SENSOR_DEFAULT_SCALE
        );

        ESP_LOGI(
            SENSOR_TAG,
            "Monitoreo iniciado."
        );
    }


    last_log_time = xTaskGetTickCount();


    for (;;) {

        /*
         * Comprobamos si existe una conversión
         * disponible.
         */
        if (!hx711_is_ready(&s_hx711)) {
            vTaskDelay(
                pdMS_TO_TICKS(SENSOR_LOOP_DELAY_MS)
            );

            continue;
        }


        /*
         * Leer tres muestras y obtener mediana.
         */
        float psi;

        err = sensor_read_median(&psi);


        if (err != ESP_OK) {
            vTaskDelay(
                pdMS_TO_TICKS(SENSOR_LOOP_DELAY_MS)
            );

            continue;
        }


        /*
         * Eliminar valores cercanos a cero.
         */
        if (psi > -0.8f && psi < 0.8f) {

            psi = 0.0f;
        }

        if (psi < 0.0f) {

            psi = 0.0f;
        }


        /*
         * Limitar rango físico.
         */
        if (psi > MAX_RANGE_PSI) {

            ESP_LOGW(
                SENSOR_TAG,
                "Lectura fuera de rango descartada: %.2f PSI",
                (double)psi
            );


            vTaskDelay(
                pdMS_TO_TICKS(SENSOR_LOOP_DELAY_MS)
            );

            continue;
        }

        /*
         * Filtro de salto sospechoso.
         */
        if (fabsf(psi - previous_pressure)
            > SUSPICIOUS_JUMP_PSI) {

            consecutive_jumps++;


            ESP_LOGW(
                SENSOR_TAG,
                "Salto sospechoso: %.2f -> %.2f PSI (%u/%u)",
                (double)previous_pressure,
                (double)psi,
                consecutive_jumps,
                REQUIRED_CONFIRMATIONS
            );

            if (consecutive_jumps
                < REQUIRED_CONFIRMATIONS) {

                vTaskDelay(
                    pdMS_TO_TICKS(SENSOR_LOOP_DELAY_MS)
                );

                continue;
            }
        }
        else {

            /*
             * Lectura normal:
             * reiniciar contador.
             */
            consecutive_jumps = 0;
        }


        /*
         * Guardar presión válida.
         */
        previous_pressure = psi;


        /*
         * Comprobar umbral.
         */
        bool is_alert = (psi > s_threshold);

        s_pressure = psi;
        s_alert = is_alert;
        
        /*
         * LED Integrado (GPIO).
         */
        esp_err_t led_err = led_set_alert(is_alert);

        /*
         * MOSFET.
         */
        gpio_set_level(
            SENSOR_OUTPUT_PIN,
            is_alert ? 1 : 0
        );


        /*
         * Log cada segundo.
         */
        TickType_t current_time =
            xTaskGetTickCount();


        if (
            current_time - last_log_time
            >= pdMS_TO_TICKS(SENSOR_LOG_INTERVAL_MS)
        ) {

            ESP_LOGI(
                SENSOR_TAG,
                "Pressure: %.2f PSI | "
                "Threshold: %.2f PSI | "
                "Alert: %s | "
                "MOSFET GPIO%d: %s",

                (double)psi,

                (double)s_threshold,

                is_alert
                    ? "ACTIVE"
                    : "inactive",

                SENSOR_OUTPUT_PIN,

                is_alert
                    ? "ON"
                    : "OFF"
            );


            last_log_time = current_time;
        }

        vTaskDelay(
            pdMS_TO_TICKS(SENSOR_LOOP_DELAY_MS)
        );
    }
}


/*
 * Inicialización.
 */
esp_err_t sensor_init(void)
{
    s_pressure = 0.0f;

    s_threshold =
        SENSOR_DEFAULT_UMBRAL;

    s_alert = false;

    previous_pressure = 0.0f;

    consecutive_jumps = 0;


    /*
     * GPIO SCK.
     */
    gpio_reset_pin(SENSOR_SCK_PIN);


    /*
     * GPIO DOUT.
     */
    gpio_reset_pin(SENSOR_DOUT_PIN);


    /*
     * GPIO MOSFET.
     */
    gpio_reset_pin(SENSOR_OUTPUT_PIN);


    /*
     * Configurar MOSFET.
     */
    gpio_config_t out_cfg = {
        .pin_bit_mask =
            (1ULL << SENSOR_OUTPUT_PIN),

        .mode =
            GPIO_MODE_OUTPUT,

        .pull_up_en =
            GPIO_PULLUP_DISABLE,

        .pull_down_en =
            GPIO_PULLDOWN_DISABLE,

        .intr_type =
            GPIO_INTR_DISABLE,
    };


    esp_err_t err =
        gpio_config(&out_cfg);


    if (err != ESP_OK) {

        ESP_LOGE(
            SENSOR_TAG,
            "Error configurando GPIO%d: %s",
            SENSOR_OUTPUT_PIN,
            esp_err_to_name(err)
        );

        return err;
    }


    gpio_set_level( SENSOR_OUTPUT_PIN, 0 );

    /*
     * LED.
     */
    err = led_init();


    if (err != ESP_OK) {

        ESP_LOGW(
            SENSOR_TAG,
            "No se pudo iniciar LED WS2812"
        );
    }


    /*
     * Inicializar HX711.
     */
    ESP_LOGI(
        SENSOR_TAG,
        "HX711 configurado: "
        "OUT/DOUT=GPIO%d, "
        "SCK=GPIO%d",

        SENSOR_DOUT_PIN,
        SENSOR_SCK_PIN
    );


    err =
        hx711_init(
            &s_hx711,
            SENSOR_DOUT_PIN,
            SENSOR_SCK_PIN
        );


    if (err != ESP_OK) {

        ESP_LOGE(
            SENSOR_TAG,
            "Error inicializando HX711: %s",
            esp_err_to_name(err)
        );

        return err;
    }


    return ESP_OK;
}


/*
 * Iniciar tarea.
 */
esp_err_t sensor_start(void)
{
    BaseType_t ret =
        xTaskCreate(
            sensor_task,
            "SensorTask",
            SENSOR_TASK_STACK,
            NULL,
            SENSOR_TASK_PRIORITY,
            NULL
        );


    if (ret != pdPASS) {

        ESP_LOGE(
            SENSOR_TAG,
            "No se pudo crear SensorTask"
        );

        return ESP_FAIL;
    }


    return ESP_OK;
}


/*
 * API pública.
 */
float sensor_get_pressure(void)
{
    return s_pressure;
}


float sensor_get_threshold(void)
{
    return s_threshold;
}


void sensor_set_threshold(float psi)
{
    s_threshold = psi;
}


void sensor_set_scale(float escala)
{
    hx711_set_scale(
        &s_hx711,
        escala
    );
}

bool sensor_get_alert(void)
{
    return s_alert;
}