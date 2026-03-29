#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "adxl345.h"
#include "buffers.h"
#include "wifi.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "mqtt.h"
#include "websocket.h"
#include "fft.h"
#include "fft_test.h"
#include "esp_private/esp_clk.h"

#include "esp_system.h"
#include "esp_chip_info.h"
static const char *TAG = "MAIN";
void wifi_rssi_task(void *pvParameters)
{
    int8_t power = 0;         // текущая мощность
    wifi_ap_record_t ap_info; // для получения RSSI

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(5000));

        // --- Получаем уровень сигнала (RSSI) ---
        esp_wifi_sta_get_ap_info(&ap_info);
        ESP_LOGI("wifi", "Connected to %s, RSSI: %d dBm", ap_info.ssid, ap_info.rssi);

        // --- Получаем текущую мощность передатчика ---
        esp_err_t err = esp_wifi_get_max_tx_power(&power);
        if (err == ESP_OK)
        {
            // Мощность в единицах по 0.25 dBm. Переведём в dBm для наглядности.
            float power_dbm = power * 0.25f;
            ESP_LOGI("wifi", "Current TX power: set value = %d (%.2f dBm)", power, power_dbm);
        }
        else
        {
            ESP_LOGE("wifi", "Failed to get TX power, error: %d", err);
        }
    }
}

void app_main(void)
{
    int cpu_freq_hz = esp_clk_cpu_freq();
    int cpu_freq_mhz = cpu_freq_hz / 1000000;
    ESP_LOGI("CPU", "Текущая частота: %d МГц", cpu_freq_mhz);

    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    ESP_LOGI(TAG, "Model: %d", chip_info.model);

    ESP_LOGI(TAG, "Revision: %d", chip_info.revision);
    // esp_log_level_set("*", ESP_LOG_INFO);
    vTaskDelay(pdMS_TO_TICKS(500));
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
    vTaskDelay(pdMS_TO_TICKS(500));
    // mqtt_app_start();
    websocket_app_start();

    buffers_init();

    xTaskCreatePinnedToCore(adxl345_read_axes, "read_axes", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(websocket_send_spectrum_task, "wifi_send", 8192, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(fft_task, "FFT", 8192, NULL, 4, NULL, 0);
    // xTaskCreatePinnedToCore(fft_test_task, "FFT_TEST", 8192, NULL, 4, NULL, 0);
}