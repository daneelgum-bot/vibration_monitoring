#include "new_buffers.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <math.h>
#include "websocket.h"
#include "param.h"
#include "esp_timer.h"

static const char *TAG = "BUFFERS";
extern volatile bool g_cycle_active;

// Статический массив буферов
static adxl345_buffer_t s_buffers[NUM_BUFFERS];

// Очереди
QueueHandle_t s_free_queue = NULL;
QueueHandle_t s_data_queue = NULL;
QueueHandle_t s_spectrum_queue = NULL;

// Инициализация очередей и буферов
void buffers_init(void)
{
    s_free_queue = xQueueCreate(NUM_BUFFERS, sizeof(adxl345_buffer_t *));
    s_data_queue = xQueueCreate(NUM_BUFFERS, sizeof(adxl345_buffer_t *));
    s_spectrum_queue = xQueueCreate(NUM_BUFFERS, sizeof(adxl345_buffer_t *));

    assert(s_free_queue && s_data_queue && s_spectrum_queue);

    for (int i = 0; i < NUM_BUFFERS; i++)
    {
        adxl345_buffer_t *buf = &s_buffers[i];
        xQueueSend(s_free_queue, &buf, 0);
    }
    ESP_LOGI(TAG, "Buffers initialized: %d total", NUM_BUFFERS);
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
    read_task_param_t *param = (read_task_param_t *)pvParameters;
    adxl345_t *dev = param->dev;
    uint8_t sensor_id = param->sensor_id;

    // Таймер с периодом 313 микросекунд
    esp_timer_handle_t timer;
    esp_timer_create_args_t timer_args = {
        .callback = &timer_callback,
        .arg = xTaskGetCurrentTaskHandle(),
        .name = (sensor_id == 1) ? "adxl_timer1" : "adxl_timer2"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 313)); // микросекунды!

    uint8_t *rx_buffer = heap_caps_malloc(7, MALLOC_CAP_DMA);
    uint8_t *tx_buffer = heap_caps_malloc(7, MALLOC_CAP_DMA);
    assert(rx_buffer && tx_buffer);

    tx_buffer[0] = 0xC0 | 0x32;
    for (int i = 1; i < 7; i++)
        tx_buffer[i] = 0xFF;

    spi_transaction_t t = {
        .length = 8 * 7,
        .tx_buffer = tx_buffer,
        .rx_buffer = rx_buffer};

    adxl345_buffer_t *current_buf = NULL;
    xQueueReceive(s_free_queue, &current_buf, portMAX_DELAY);
    current_buf->sensor_id = sensor_id;

    int idx = 0;
    while (1)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // ждём тик таймера

        if (!g_cycle_active)
        {
            continue; // Пропускаем измерение, если станок не в цикле
        }
        spi_device_transmit(dev->handle, &t);

        int16_t raw_x = (int16_t)(rx_buffer[1] | (rx_buffer[2] << 8));
        int16_t raw_y = (int16_t)(rx_buffer[3] | (rx_buffer[4] << 8));
        int16_t raw_z = (int16_t)(rx_buffer[5] | (rx_buffer[6] << 8));

        float ax = raw_x * 0.004f;
        float ay = raw_y * 0.004f;
        float az = raw_z * 0.004f;
        float module_g = sqrtf(ax * ax + ay * ay + az * az);

        current_buf->accel[idx++] = module_g;

        if (idx >= BUF_SIZE)
        {
            if (xQueueSend(s_data_queue, &current_buf, 0) != pdTRUE)
            {
                ESP_LOGE(TAG, "Data queue full, dropping (sensor %d)", sensor_id);
                xQueueSend(s_free_queue, &current_buf, 0);
            }
            xQueueReceive(s_free_queue, &current_buf, portMAX_DELAY);
            current_buf->sensor_id = sensor_id;
            idx = 0;
        }
    }
}

void websocket_send_spectrum_task(void *pvParameters)
{
    // Подключаем ваш модуль websocket
    extern esp_websocket_client_handle_t websocket_app_get_client(void);

    adxl345_buffer_t *buf;
    const size_t packet_size = sizeof(adxl345_buffer_t);

    while (1)
    {
        if (xQueueReceive(s_spectrum_queue, &buf, portMAX_DELAY) == pdTRUE && buf != NULL)
        {
            esp_websocket_client_handle_t ws = websocket_app_get_client();
            if (ws && esp_websocket_client_is_connected(ws))
            {
                int sent = esp_websocket_client_send_bin(ws, (const char *)buf,
                                                         packet_size, pdMS_TO_TICKS(100));
                if (sent > 0)
                {
                    ESP_LOGI(TAG, "Sent spectrum from sensor %d, %d bytes",
                             (int)buf->sensor_id, sent);
                }
                else
                {
                    ESP_LOGE(TAG, "WebSocket send failed");
                }
            }
            // Возвращаем буфер в пул свободных
            xQueueSend(s_free_queue, &buf, 0);
        }
    }
}


