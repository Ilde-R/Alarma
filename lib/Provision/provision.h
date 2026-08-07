#ifndef PROVISION_H
#define PROVISION_H

#include "esp_err.h"

esp_err_t provision_start(void);

esp_err_t provision_http_start(void);

void provision_http_stop(void);

#endif
