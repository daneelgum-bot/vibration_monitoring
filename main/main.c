#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "new_adxl_two.h"
#include "new_buffers.h"
//#include "wifi.h"        // ваша инициализация Wi‑Fi
#include "websocket.h"   // ваш модуль WebSocket
#include "ethernet.h"
#include "new_fft.h"
#include "driver/gpio.h"

static const char *TAG = "MAIN";

// Пины для первой шины SPI2 (датчик 1)
#define SPI2_MISO   12 //фиол SDO
#define SPI2_MOSI   13 //оранж SDA
#define SPI2_SCLK   14
#define SPI2_CS1    32 //замена 32

// Пины для второй шины (датчик 2)
#define SPI3_MISO   5 //замена на 5 (33)
#define SPI3_MOSI   16
#define SPI3_SCLK   4
#define SPI3_CS2    15


#define CYCLE_PIN GPIO_NUM_34
volatile bool g_cycle_active = false;

static void IRAM_ATTR cycle_isr_handler(void *arg)
{
    // При использовании подтяжки к GND (pull-down)
    g_cycle_active = (gpio_get_level(CYCLE_PIN) == 1);
}


void app_main(void)
{

    gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << CYCLE_PIN),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE, // Внутренние подтяжки не работают
    .intr_type = GPIO_INTR_ANYEDGE
};
gpio_config(&io_conf);
gpio_install_isr_service(0);
gpio_isr_handler_add(CYCLE_PIN, cycle_isr_handler, NULL);


if (gpio_get_level(CYCLE_PIN) == 1) {
    ESP_LOGI(TAG, "Pin %d is already high, triggering event manually.", CYCLE_PIN);
    g_cycle_active = true; // Устанавливаем флаг вручную
    // Если бы вы использовали очередь, то отправили бы событие в нее
}

    // Инициализация NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(adxl345_bus_init(SPI2_HOST, SPI2_MISO, SPI2_MOSI, SPI2_SCLK));
    ESP_ERROR_CHECK(adxl345_bus_init(SPI3_HOST, SPI3_MISO, SPI3_MOSI, SPI3_SCLK));

    // Создаём два устройства
    static adxl345_t dev1, dev2;
    ESP_ERROR_CHECK(adxl345_init(&dev1, SPI2_HOST, SPI2_CS1));
    ESP_ERROR_CHECK(adxl345_init(&dev2, SPI3_HOST, SPI3_CS2));

    adxl345_force_4wire_spi(&dev1);
    adxl345_force_4wire_spi(&dev2);

    adxl345_configure(&dev1);
    adxl345_configure(&dev2);

 
    if (!adxl345_check_presence(&dev1) || !adxl345_check_presence(&dev2)) {
        ESP_LOGE(TAG, "One or both ADXL345 not found!");
        return;
    }

    // Инициализация буферов и очередей
    buffers_init();

    // Параметры для задач чтения
    static read_task_param_t param1 = { .dev = &dev1, .sensor_id = 1 };
    static read_task_param_t param2 = { .dev = &dev2, .sensor_id = 2 };
    ethernet_init();
    vTaskDelay(pdMS_TO_TICKS(1000));
    websocket_app_start();
    // Задачи чтения (по одной на каждый датчик)
    xTaskCreatePinnedToCore(adxl345_read_axes, "read1", 4096, &param1, 5, NULL, 1);
    xTaskCreatePinnedToCore(adxl345_read_axes, "read2", 4096, &param2, 5, NULL, 1);

    // Задача БПФ (обрабатывает данные из s_data_queue)
    xTaskCreatePinnedToCore(fft_task, "FFT", 8192, NULL, 4, NULL, 0);

    // Задача отправки спектра через WebSocket
    xTaskCreatePinnedToCore(websocket_send_spectrum_task, "ws_send", 8192, NULL, 3, NULL, 0);

    ESP_LOGI(TAG, "System started with 2 ADXL345 sensors on separate SPI buses");
}