#include "websocket.h"
#include "esp_log.h"

static const char *TAG = "websocket_app";
static esp_websocket_client_handle_t websocket_client = NULL;

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket Connected");
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WebSocket Disconnected");
        break;
    case WEBSOCKET_EVENT_DATA:
        ESP_LOGI(TAG, "Received data from server");
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WebSocket Error");
        break;
    default:
        break;
    }
}

void websocket_app_start(void)
{
    esp_websocket_client_config_t ws_cfg = {
        .uri = "ws://192.168.0.131:8765", // 
        .buffer_size = 8192,         
        .task_stack = 8192,
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms = 10000,  
    };

    websocket_client = esp_websocket_client_init(&ws_cfg);
    esp_websocket_register_events(websocket_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL);
    esp_websocket_client_start(websocket_client);
}

esp_websocket_client_handle_t websocket_app_get_client(void)
{
    return websocket_client;
}
