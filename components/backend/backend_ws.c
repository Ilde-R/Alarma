#include <stdio.h>
#include <string.h>
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_transport_tcp.h"
#include "esp_transport_ws.h"
#include "backend_ws.h"

#define BACKEND_TAG "BACKEND_WS"
#define BACKEND_HOST "78.13.219.157"
#define BACKEND_PORT 3000
#define BACKEND_CONNECT_TIMEOUT_MS 10000
#define BACKEND_POLL_TIMEOUT_MS 50
#define BACKEND_READ_TIMEOUT_MS 5000
#define BACKEND_SEND_TIMEOUT_MS 3000

static bool send_frame(backend_ws_t* client, uint8_t opcode, const char* payload) {
    if (client == NULL || client->ws == NULL || client->transport == NULL || !client->connected) {
        return false;
    }

    size_t length = strlen(payload);
    if (length > 65535) {
        ESP_LOGE(BACKEND_TAG, "Payload WS demasiado largo (%u bytes)", (unsigned) length);
        return false;
    }

    uint8_t frame[8];
    size_t header_length = 0;
    frame[header_length++] = 0x80 | opcode;
    if (length <= 125) {
        frame[header_length++] = 0x80 | (uint8_t) length;
    } else {
        frame[header_length++] = 0x80 | 126;
        frame[header_length++] = (uint8_t) (length >> 8);
        frame[header_length++] = (uint8_t) length;
    }

    uint32_t mask = esp_random();
    frame[header_length++] = (uint8_t) (mask >> 24);
    frame[header_length++] = (uint8_t) (mask >> 16);
    frame[header_length++] = (uint8_t) (mask >> 8);
    frame[header_length++] = (uint8_t) mask;

    if (esp_transport_write(client->transport, (const char*) frame, (int) header_length,
                            BACKEND_SEND_TIMEOUT_MS) != (int) header_length) {
        return false;
    }

    uint8_t mask_bytes[4] = {
        (uint8_t) (mask >> 24),
        (uint8_t) (mask >> 16),
        (uint8_t) (mask >> 8),
        (uint8_t) mask,
    };
    size_t offset = 0;
    uint8_t chunk[128];
    while (offset < length) {
        size_t count = length - offset;
        if (count > sizeof(chunk)) {
            count = sizeof(chunk);
        }
        for (size_t i = 0; i < count; i++) {
            chunk[i] = (uint8_t) payload[offset + i] ^ mask_bytes[(offset + i) % 4];
        }
        if (esp_transport_write(client->transport, (const char*) chunk, (int) count,
                                BACKEND_SEND_TIMEOUT_MS) != (int) count) {
            return false;
        }
        offset += count;
    }
    return true;
}

void backend_ws_init(backend_ws_t* client) {
    if (client != NULL) {
        memset(client, 0, sizeof(*client));
    }
}

void backend_ws_close(backend_ws_t* client) {
    if (client == NULL) {
        return;
    }
    if (client->ws != NULL) {
        esp_transport_close(client->ws);
        esp_transport_destroy(client->ws);
        client->ws = NULL;
    }
    if (client->transport != NULL) {
        esp_transport_destroy(client->transport);
        client->transport = NULL;
    }
    client->connected = false;
}

esp_err_t backend_ws_connect(backend_ws_t* client, const char* device_key) {
    if (client == NULL || device_key == NULL || device_key[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    backend_ws_close(client);
    client->transport = esp_transport_tcp_init();
    if (client->transport == NULL) {
        return ESP_FAIL;
    }

    client->ws = esp_transport_ws_init(client->transport);
    if (client->ws == NULL) {
        backend_ws_close(client);
        return ESP_FAIL;
    }

    char path[128];
    snprintf(path, sizeof(path), "/?key=%s", device_key);
    esp_transport_ws_set_path(client->ws, path);
    esp_transport_ws_config_t ws_config = {
        .propagate_control_frames = true,
    };
    esp_transport_ws_set_config(client->ws, &ws_config);

    char headers[192];
    snprintf(headers, sizeof(headers), "key: %s\r\nOrigin: http://%s\r\n", device_key, BACKEND_HOST);
    esp_transport_ws_set_headers(client->ws, headers);
    esp_transport_ws_set_user_agent(client->ws, "Alarma/1.0.0");

    if (esp_transport_connect(client->ws, BACKEND_HOST, BACKEND_PORT,
                              BACKEND_CONNECT_TIMEOUT_MS) != 0) {
        backend_ws_close(client);
        return ESP_FAIL;
    }
    if (esp_transport_ws_get_upgrade_request_status(client->ws) != 101) {
        backend_ws_close(client);
        return ESP_FAIL;
    }

    client->connected = true;
    return ESP_OK;
}

bool backend_ws_is_connected(const backend_ws_t* client) {
    return client != NULL && client->connected;
}

bool backend_ws_send_text(backend_ws_t* client, const char* payload) {
    return send_frame(client, WS_TRANSPORT_OPCODES_TEXT, payload);
}

int backend_ws_read_message(backend_ws_t* client, char* buffer, int buffer_size) {
    if (client == NULL || buffer == NULL || buffer_size < 2 || !client->connected) {
        return -1;
    }
    int poll = esp_transport_poll_read(client->ws, BACKEND_POLL_TIMEOUT_MS);
    if (poll == 0) {
        return 0;
    }
    if (poll < 0) {
        return -1;
    }
    int length = esp_transport_read(client->ws, buffer, buffer_size - 1, BACKEND_READ_TIMEOUT_MS);
    if (length <= 0) {
        return length;
    }
    buffer[length] = '\0';
    ws_transport_opcodes_t opcode = esp_transport_ws_get_read_opcode(client->ws);
    if (opcode == WS_TRANSPORT_OPCODES_PING) {
        send_frame(client, WS_TRANSPORT_OPCODES_PONG, buffer);
        return 0;
    }
    if (opcode == WS_TRANSPORT_OPCODES_CLOSE) {
        return -2;
    }
    return opcode == WS_TRANSPORT_OPCODES_TEXT ? length : 0;
}
