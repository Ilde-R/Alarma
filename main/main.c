#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "backend.h"
#include "network.h"
#include "provision.h"
#include "sensor.h"

static void wifi_rescue_task(void* arg) {
    provision_start();
    vTaskDelete(NULL);
}

static void wifi_rescue_handler(void) {
    xTaskCreate(wifi_rescue_task, "RescueTask", 4096, NULL, 1, NULL);
}

void app_main(void) {
    sensor_init();
    sensor_start();

    network_init();
    network_set_rescue_callback(wifi_rescue_handler);

    if (network_get_status() == NETWORK_NEEDS_PROVISIONING) {
        provision_start();
    } else {
        backend_init();
        backend_start();
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
