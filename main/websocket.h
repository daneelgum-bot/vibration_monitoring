#pragma once
#include "esp_websocket_client.h"

esp_websocket_client_handle_t websocket_app_get_client(void);
void websocket_app_start(void);
