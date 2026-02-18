#include "buffers.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <math.h>
#include "driver/spi_master.h"
#include "freertos/task.h" 

static const char *TAG = "TASKS";


static adxl345_axes_t s_buffer1[BUF_SIZE];
static adxl345_axes_t s_buffer2[BUF_SIZE];

static QueueHandle_t s_free_queue = NULL; // очередь свободных буферов
static QueueHandle_t s_data_queue = NULL; // очередь заполненных буферов

static void timer_callback(void *arg)
{
    TaskHandle_t task = (TaskHandle_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(task, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}



void buffers_init(void) {
    s_free_queue = xQueueCreate(2, sizeof(adxl345_axes_t *));
    s_data_queue = xQueueCreate(2, sizeof(adxl345_axes_t *));
    if (s_free_queue == NULL || s_data_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queues");
        return;
    }

    adxl345_axes_t *buf_ptrs[2] = {s_buffer1, s_buffer2};
    for (int i = 0; i < 2; i++) {
        if (xQueueSend(s_free_queue, &buf_ptrs[i], 0) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to send buffer %d to free_queue", i);
        } else {
            ESP_LOGI(TAG, "Buffer %d sent to free_queue: %p", i, buf_ptrs[i]);
        }
    }
}


void adxl345_read_axes(void *pvParameters) {
    // Выделение DMA-памяти
    uint8_t *rx_buffer = heap_caps_malloc(7, MALLOC_CAP_DMA);
    uint8_t *tx_buffer = heap_caps_malloc(7, MALLOC_CAP_DMA);
    if (rx_buffer == NULL || tx_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate DMA memory");
        vTaskDelete(NULL);
        return;
    }
    tx_buffer[0] = 0xC0 | 0x32;
    for (int i = 1; i < 7; i++) tx_buffer[i] = 0xFF;

    spi_transaction_t t = {
        .length = 8 * 7,
        .tx_buffer = tx_buffer,
        .rx_buffer = rx_buffer
    };

    // Таймер [хуйня]
    esp_timer_handle_t timer;
    esp_timer_create_args_t timer_args = {
        .callback = &timer_callback,
        .arg = xTaskGetCurrentTaskHandle(),
        .name = "adxl_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 312));

    // Берём первый свободный буфер из внутренней очереди
    adxl345_axes_t *current_buf;
    if (xQueueReceive(s_free_queue, &current_buf, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to get free buffer");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Initial buffer: %p", current_buf);
    if (current_buf == NULL) {
        ESP_LOGE(TAG, "Buffer is NULL!");
        vTaskDelete(NULL);
        return;
    }

    int idx = 0;
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        spi_device_transmit(spi_adxl, &t);   // spi_adxl из adxl345.h (extern)

        int16_t raw_x = (int16_t)(rx_buffer[1] | (rx_buffer[2] << 8));
        int16_t raw_y = (int16_t)(rx_buffer[3] | (rx_buffer[4] << 8));
        int16_t raw_z = (int16_t)(rx_buffer[5] | (rx_buffer[6] << 8));

        current_buf[idx].x = raw_x * 0.004f;
        current_buf[idx].y = raw_y * 0.004f;
        current_buf[idx].z = raw_z * 0.004f;
        idx++;

        if (idx >= BUF_SIZE) {
            // Отправляем в очередь данных
            if (xQueueSend(s_data_queue, &current_buf, 0) != pdTRUE) {
                ESP_LOGE(TAG, "Data queue full! Data lost?");
            }
            // Получаем новый свободный буфер
            if (xQueueReceive(s_free_queue, &current_buf, portMAX_DELAY) != pdTRUE) {
                ESP_LOGE(TAG, "Failed to get free buffer");
                vTaskDelete(NULL);
                return;
            }
            if (current_buf == NULL) {
                ESP_LOGE(TAG, "Received NULL buffer from free_queue");
                vTaskDelete(NULL);
                return;
            }
            idx = 0;
        }
    }
}


void wifi_send_task(void *pvParameters) {
    adxl345_axes_t *buf;
    while (1) {
        if (xQueueReceive(s_data_queue, &buf, portMAX_DELAY) == pdTRUE) {
            if (buf == NULL) {
                ESP_LOGE(TAG, "Received NULL buffer from data_queue");
                continue;
            }

            double sum_sq = 0.0;
            for (int i = 0; i < BUF_SIZE; i++) {
                sum_sq += (double)(buf[i].x * buf[i].x +
                                   buf[i].y * buf[i].y +
                                   buf[i].z * buf[i].z);
            }
            double rms = sqrt(sum_sq / BUF_SIZE);
            ESP_LOGI(TAG, "Buffer %p RMS = %.4f g", buf, rms);
            // Возвращаем буфер в свободную очередь
            if (xQueueSend(s_free_queue, &buf, 0) != pdTRUE) {
                ESP_LOGE(TAG, "Failed to return buffer to free_queue");
            }
        }
    }
}