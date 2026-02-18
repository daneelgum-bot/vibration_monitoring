#include <stdio.h>
#include "esp_log.h"
#include "esp_check.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <math.h>
#include "freertos/queue.h"


#define ADXL345_CS_GPIO GPIO_NUM_5
#define ADXL345_SPI_HOST SPI2_HOST
#define ADXL345_INT1_GPIO GPIO_NUM_4

static const char *TAG = "ADXL345_SPI";
spi_device_handle_t spi_adxl;

typedef struct
{
    float x, y, z;
} adxl345_axes_t;

// --- Двойная буферизация ---
#define BUF_SIZE 1600
static adxl345_axes_t buffer1[BUF_SIZE];
static adxl345_axes_t buffer2[BUF_SIZE];
static QueueHandle_t free_queue = NULL; // очередь свободных буферов
static QueueHandle_t data_queue = NULL; // очередь заполненных буферов

void vApplicationGetRunTimeStats(uint32_t *pulRunTimeCounter)
{
    *pulRunTimeCounter = (uint32_t)(esp_timer_get_time() & 0xFFFFFFFF);
}

// ---------- Чтение одного регистра ----------
uint8_t adxl345_read_byte(uint8_t reg_addr)
{
    uint8_t tx_data[2] = {0x80 | reg_addr, 0xFF};
    uint8_t rx_data[2];
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data};
    spi_device_transmit(spi_adxl, &t);
    return rx_data[1];
}

// ---------- Запись одного регистра ----------
void adxl345_write_byte(uint8_t reg_addr, uint8_t data)
{
    uint8_t tx_data[2] = {reg_addr & 0x3F, data};
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx_data};
    spi_device_transmit(spi_adxl, &t);
}

// ---------- Инициализация SPI ----------
void adxl345_init_spi(void)
{
    spi_bus_config_t buscfg = {
        .miso_io_num = GPIO_NUM_19,
        .mosi_io_num = GPIO_NUM_23,
        .sclk_io_num = GPIO_NUM_18,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 10};

    spi_device_interface_config_t devcfg = {
        .mode = 3,
        .clock_speed_hz = 1.5e6,
        .spics_io_num = ADXL345_CS_GPIO,
        .queue_size = 7,
    };

    ESP_ERROR_CHECK(spi_bus_initialize(ADXL345_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(ADXL345_SPI_HOST, &devcfg, &spi_adxl));
    ESP_LOGI(TAG, "SPI initialized");
}

// ---------- Конфигурация датчика ----------
void adxl345_configure(void)
{
    adxl345_write_byte(0x2D, 0x08); // POWER_CTL: включить измерения
    adxl345_write_byte(0x31, 0x09); // DATA_FORMAT: +-4g, FULL_RES=1
    adxl345_write_byte(0x2C, 0x0F); // DATA_RATE = 3200 Гц
}

static void timer_callback(void *arg)
{
    TaskHandle_t task = (TaskHandle_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(task, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void adxl345_read_axes(void *pvParameters)
{
    // выделение DMA‑памяти для SPI
    uint8_t *rx_buffer = heap_caps_malloc(7, MALLOC_CAP_DMA);
    uint8_t *tx_buffer = heap_caps_malloc(7, MALLOC_CAP_DMA);
    if (rx_buffer == NULL || tx_buffer == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate DMA memory");
        vTaskDelete(NULL);
        return;
    }
    tx_buffer[0] = 0xC0 | 0x32; // чтение + многобайтный режим с адреса 0x32
    for (int i = 1; i < 7; i++)
        tx_buffer[i] = 0xFF;

    spi_transaction_t t = {
        .length = 8 * 7,
        .tx_buffer = tx_buffer,
        .rx_buffer = rx_buffer};

    // Таймер для периодического чтения хуйни
    esp_timer_handle_t timer;
    esp_timer_create_args_t timer_args = {
        .callback = &timer_callback,
        .arg = xTaskGetCurrentTaskHandle(),
        .name = "adxl_timer"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 312));

    // Берём первый свободный буфер
    adxl345_axes_t *current_buf;
    if (xQueueReceive(free_queue, &current_buf, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "оШИБКА ПОЛУЧЕНИЯ СВОБОДНОГО БУФЕРА");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "ИНИЦИЛИЗАЦИЯ БУФЕРА: %p", current_buf);
    if (current_buf == NULL)
    {
        ESP_LOGE(TAG, "ИНИЦИАЛИЗИРОВАННЫЙ БУФЕР = NULL!");
        vTaskDelete(NULL);
        return;
    }

    int idx = 0;
    while (1)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        spi_device_transmit(spi_adxl, &t);

        int16_t raw_x = (int16_t)(rx_buffer[1] | (rx_buffer[2] << 8));
        int16_t raw_y = (int16_t)(rx_buffer[3] | (rx_buffer[4] << 8));
        int16_t raw_z = (int16_t)(rx_buffer[5] | (rx_buffer[6] << 8));

        current_buf[idx].x = raw_x * 0.004f;
        current_buf[idx].y = raw_y * 0.004f;
        current_buf[idx].z = raw_z * 0.004f;
        idx++;

        if (idx >= BUF_SIZE)
        {
            // Буфер заполнен -> отправляем в очередь данных
            if (xQueueSend(data_queue, &current_buf, 0) != pdTRUE)
            {
                ESP_LOGE(TAG, "Data queue full! Data lost?");
            }
            // Получаем новый свободный буфер
            if (xQueueReceive(free_queue, &current_buf, portMAX_DELAY) != pdTRUE)
            {
                ESP_LOGE(TAG, "ОШИБКА ПОЛУЧЕНИЯ СВОБОДНОГО БУФЕРА");
                vTaskDelete(NULL);
                return;
            }
            if (current_buf == NULL)
            {
                ESP_LOGE(TAG, "Received NULL buffer from free_queue");
                vTaskDelete(NULL);
                return;
            }
            idx = 0;
        }
    }
}

// ---------- обработчик(потребитель, заглушка) ----------
void wifi_send_task(void *pvParameters)
{
    adxl345_axes_t *buf;
    while (1)
    {
        if (xQueueReceive(data_queue, &buf, portMAX_DELAY) == pdTRUE) // получение буфера
        {
            if (buf == NULL)
            {
                ESP_LOGE(TAG, "Received NULL buffer from data_queue");
                continue;
            }

            double sum_sq = 0.0;
            for (int i = 0; i < BUF_SIZE; i++)
            {
                sum_sq += (double)(buf[i].x * buf[i].x +
                                   buf[i].y * buf[i].y +
                                   buf[i].z * buf[i].z);
            }
            double rms = sqrt(sum_sq / BUF_SIZE);
            ESP_LOGI(TAG, "Buffer %p RMS = %.4f g", buf, rms);
            // Возвращаем буфер в очередь свободных
            if (xQueueSend(free_queue, &buf, 0) != pdTRUE)
            {
                ESP_LOGE(TAG, "Failed to return buffer to free_queue");
            }
        }
    }
}

// ---------- Принудительный 4-проводной SPI ----------
void adxl345_force_4wire_spi(void)
{
    uint8_t data_format = adxl345_read_byte(0x31);
    data_format &= ~0x40;
    adxl345_write_byte(0x31, data_format);
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "DATA_FORMAT: 0x%02X", adxl345_read_byte(0x31));
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    adxl345_init_spi();
    adxl345_force_4wire_spi();
    vTaskDelay(pdMS_TO_TICKS(50));

    uint8_t devid = adxl345_read_byte(0x00);
    if (devid != 0xE5)
    {
        ESP_LOGE(TAG, "ADXL345 not found! ID=0x%02X", devid);
        return;
    }
    ESP_LOGI(TAG, "ADXL345 found! ID=0x%02X", devid);
    adxl345_configure();

    // очереди для буферов
    free_queue = xQueueCreate(2, sizeof(adxl345_axes_t *));
    data_queue = xQueueCreate(2, sizeof(adxl345_axes_t *));
    if (free_queue == NULL || data_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create queues");
        return;
    }

    // Корректная отправка указателей на буферы в очередь свободных
    adxl345_axes_t *buf_ptrs[2] = {buffer1, buffer2};
    for (int i = 0; i < 2; i++)
    {
        if (xQueueSend(free_queue, &buf_ptrs[i], 0) != pdTRUE)
        {
            ESP_LOGE(TAG, "Failed to send buffer %d to free_queue", i);
        }
        else
        {
            ESP_LOGI(TAG, "Buffer %d sent to free_queue: %p", i, buf_ptrs[i]);
        }
    }

    xTaskCreatePinnedToCore(adxl345_read_axes, "read_axes", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(wifi_send_task, "wifi_send", 4096, NULL, 5, NULL, 0);
}