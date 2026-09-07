#ifndef BACKEND_PROTOCOL_H
#define BACKEND_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>
#include "backend_config.h"
#include "backend_ws.h"

void backend_protocol_send_pressure(backend_ws_t* ws, int64_t timestamp_ms);
void backend_protocol_send_device_info(backend_ws_t* ws, const char* device_key);
void backend_protocol_handle_message(backend_ws_t* ws, const char* message,
                                     backend_config_t* config,
                                     bool* credentials_invalid);

#endif
