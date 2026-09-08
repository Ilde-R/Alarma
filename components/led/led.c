#include <string.h>
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"

#define LED_TAG "LED"

#define LED_RESOLUTION_HZ 10000000
#define LED_MEM_SYMBOLS 48
#define LED_TX_QUEUE_DEPTH 4

#define LED_T0H 3
#define LED_T0L 9
#define LED_T1H 9
#define LED_T1L 3
static rmt_channel_handle_t s_tx_chan = NULL;
static rmt_encoder_handle_t s_encoder = NULL;

esp_err_t led_init(void) {
    rmt_tx_channel_config_t tx_cfg = {
        .gpio_num = LED_WS2812_PIN,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LED_RESOLUTION_HZ,
        .mem_block_symbols = LED_MEM_SYMBOLS,
        .trans_queue_depth = LED_TX_QUEUE_DEPTH,
    };
    esp_err_t err = rmt_new_tx_channel(&tx_cfg, &s_tx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(LED_TAG, "Fallo al crear canal RMT: %s", esp_err_to_name(err));
        return err;
    }

    rmt_bytes_encoder_config_t enc_cfg = {
        .bit0 = {
            .level0 = 1,
            .duration0 = LED_T0H,
            .level1 = 0,
            .duration1 = LED_T0L,
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = LED_T1H,
            .level1 = 0,
            .duration1 = LED_T1L,
        },
        .flags.msb_first = true,
    };
    err = rmt_new_bytes_encoder(&enc_cfg, &s_encoder);
    if (err != ESP_OK) {
        ESP_LOGE(LED_TAG, "Fallo al crear encoder RMT: %s", esp_err_to_name(err));
        return err;
    }

    err = rmt_enable(s_tx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(LED_TAG, "Fallo al habilitar canal RMT: %s", esp_err_to_name(err));
        return err;
    }

    led_set_color(0, 0, 0);
    ESP_LOGI(LED_TAG, "LED WS2812 listo en GPIO %d", LED_WS2812_PIN);
    return ESP_OK;
}

esp_err_t led_set_color(uint8_t red, uint8_t green, uint8_t blue) {
    if (s_tx_chan == NULL || s_encoder == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t grb[3] = { green, red, blue };
    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
        .flags.eot_level = 0,
    };
    esp_err_t err = rmt_transmit(s_tx_chan, s_encoder, grb, sizeof(grb), &tx_cfg);
    if (err != ESP_OK) {
        return err;
    }
    return rmt_tx_wait_all_done(s_tx_chan, 100);
}

esp_err_t led_set_alert(bool alert) {
    return led_set_color(0, 0, alert ? 255 : 0);
}
