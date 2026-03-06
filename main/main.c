#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "adxl345.h"
#include "buffers.h"
#include "wifi.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "mqtt.h"
#include "websocket.h"

void app_main(void) {
   
    esp_log_level_set("*", ESP_LOG_INFO);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    

    adxl345_init_spi();
    adxl345_force_4wire_spi();
    vTaskDelay(pdMS_TO_TICKS(50));

    if (!adxl345_check_presence())
    {
        return;
    }
    adxl345_configure();

    ret = wifi_init_sta();
    if (ret != ESP_OK)
    {
        ESP_LOGE("wifi", "Failed to connect to Wi-Fi");
    }
    else
    {
        ESP_LOGI("wifi", "Wi-Fi connected successfully");
    }

    mqtt_app_start();
    //websocket_app_start();

    buffers_init();

    //xTaskCreatePinnedToCore(adxl345_read_axes, "read_axes", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(test_mqtt, "wifi_send", 4096, NULL, 5, NULL, 0);
}
