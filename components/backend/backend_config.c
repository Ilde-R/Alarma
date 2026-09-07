#include "nvs.h"
#include "backend_config.h"

#define NVS_NAMESPACE "config"
#define NVS_KEY_UMBRAL "umbral"
#define NVS_KEY_ESCALA "escala"
#define NVS_KEY_INTERVALO "intervalo"

#define BACKEND_DEFAULT_UMBRAL 50.0f
#define BACKEND_DEFAULT_SCALE 25000.0f
#define BACKEND_DEFAULT_INTERVAL_MS 1000

void backend_config_set_defaults(backend_config_t* config) {
    if (config == NULL) {
        return;
    }

    config->threshold = BACKEND_DEFAULT_UMBRAL;
    config->scale = BACKEND_DEFAULT_SCALE;
    config->interval_ms = BACKEND_DEFAULT_INTERVAL_MS;
}

esp_err_t backend_config_load(backend_config_t* config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    backend_config_set_defaults(config);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    size_t length = sizeof(config->threshold);
    if (nvs_get_blob(handle, NVS_KEY_UMBRAL, &config->threshold, &length) != ESP_OK ||
        length != sizeof(config->threshold)) {
        config->threshold = BACKEND_DEFAULT_UMBRAL;
    }

    length = sizeof(config->scale);
    if (nvs_get_blob(handle, NVS_KEY_ESCALA, &config->scale, &length) != ESP_OK ||
        length != sizeof(config->scale)) {
        config->scale = BACKEND_DEFAULT_SCALE;
    }

    if (nvs_get_u32(handle, NVS_KEY_INTERVALO, &config->interval_ms) != ESP_OK) {
        config->interval_ms = BACKEND_DEFAULT_INTERVAL_MS;
    }

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t backend_config_save(const backend_config_t* config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, NVS_KEY_UMBRAL, &config->threshold, sizeof(config->threshold));
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, NVS_KEY_ESCALA, &config->scale, sizeof(config->scale));
    }
    if (err == ESP_OK) {
        err = nvs_set_u32(handle, NVS_KEY_INTERVALO, config->interval_ms);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}
