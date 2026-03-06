idf_component_register(SRCS "main.c" "buffers.c" "adxl345.c" "wifi.c" "mqtt.c" "websocket.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_wifi nvs_flash esp_event mqtt esp_websocket_client)
