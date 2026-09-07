#include "driver/i2c_master.h"
#include "esp_log.h"
#include "ina219.h"

#define INA219_TAG "INA219"

#define INA219_REG_CONFIG 0x00
#define INA219_REG_BUS_VOLTAJE 0x02
#define INA219_REG_POWER 0x03
#define INA219_REG_CURRENT 0x04

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_device;
static bool s_ready = false;

esp_err_t ina219_init(int sda_pin, int scl_pin, uint8_t address){
    // Configurar el bus I2C
    return ESP_OK;
}

static esp_err_t write_register(uint8_t reg, uint16_t value);
static esp_err_t read_register(uint8_t reg, uint16_t* value);

esp_err_t ina219_read(ina219_reading_t* reading){
    // Leer los registros de voltaje, corriente y potencia
    return ESP_OK;
}