#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network.h"
#include "provision.h"

void app_main(void) {
    network_init();

    if (network_get_status() == NETWORK_NEEDS_PROVISIONING) {
        provision_start();
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
