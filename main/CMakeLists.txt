idf_component_register(SRCS "fft.c" "main.c" "buffers.c" "adxl345.c" "wifi.c" "mqtt.c" "websocket.c" "fft.c" "fft_test.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_wifi nvs_flash esp_event mqtt esp_websocket_client)
