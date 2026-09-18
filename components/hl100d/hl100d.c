#include "hl100d.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char*TAG = "HL100D_DRIVER";

esp_err_t hl100d_init(hl100d_t*sensor, const hl100d_config_t*config) {
    if(sensor == NULL || config == NULL){
        ESP_LOGE(TAG, "Punteros vacios");
        return ESP_ERR_INVALID_ARG;
    }

    sensor->config=*config;
    
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = sensor->config.adc_unit,
    };

    esp_err_t err = adc_oneshot_new_unit(&init_config, &sensor->adc_handle);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Error al crear la unidad ADC");
        return err;
    }
    
    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };

    err = adc_oneshot_config_channel(sensor->adc_handle, sensor->config.adc_channel, &chan_config);
    if(err != ESP_OK){
        ESP_LOGE(TAG, "Error al configurar el canal ADC");
    }
    return err;
}

esp_err_t hl100d_read(hl100d_t*sensor, hl100d_reading_t*reading) {
    if(sensor == NULL || reading == NULL) return ESP_ERR_INVALID_ARG;

    const int NUM_SAMPLES = 16;
    uint32_t sum_raw = 0;
    int current_raw = 0;

    for (int i = 0; i < NUM_SAMPLES; i++) {
        esp_err_t err = adc_oneshot_read(sensor->adc_handle, sensor->config.adc_channel, &current_raw);
        
        if(err != ESP_OK) {
            ESP_LOGE(TAG, "Fallo de lectura en el ADC en la muestra %d", i);
            return err;
        }
        
        sum_raw += current_raw;
        
        esp_rom_delay_us(50); 
    }

    int raw_val = sum_raw / NUM_SAMPLES;

    reading->raw_adc = raw_val;

    float pin_voltage_mv = ((float)raw_val * 2500.0f) / 4095.0f;
    float amplifier_gain = 100.0f;
    float actual_sensor_voltage_mv = pin_voltage_mv / amplifier_gain;

    reading->voltage_mv = actual_sensor_voltage_mv - sensor->config.offset_mv;

    reading->pressure_kpa = (reading->voltage_mv * sensor->config.max_pressure_kpa) / sensor->config.full_scale_mv;
                 
    if(reading->pressure_kpa < 0.0f) {
        reading->pressure_kpa = 0.0f;
    }

    return ESP_OK;
}