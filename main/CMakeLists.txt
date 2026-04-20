idf_component_register(SRCS "new_fft.c" "new_buffers.c" "new_adxl_two.c" "main.c" "websocket.c" "ethernet.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_wifi nvs_flash esp_event mqtt esp_websocket_client esp_eth)