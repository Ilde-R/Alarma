#ifndef BACKEND_WS_H
#define BACKEND_WS_H

#include <stdbool.h>
#include "esp_transport.h"
#include "esp_transport_ws.h"
#include "esp_err.h"

typedef struct {
    esp_transport_handle_t ws;
    esp_transport_handle_t transport;
    bool connected;
} backend_ws_t;

void backend_ws_init(backend_ws_t* client);
esp_err_t backend_ws_connect(backend_ws_t* client, const char* device_key);
void backend_ws_close(backend_ws_t* client);
bool backend_ws_is_connected(const backend_ws_t* client);
bool backend_ws_send_text(backend_ws_t* client, const char* payload);
int backend_ws_read_message(backend_ws_t* client, char* buffer, int buffer_size);

#endif
