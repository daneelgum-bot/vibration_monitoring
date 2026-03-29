#include "buffers.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <math.h>
#include "driver/spi_master.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "mqtt.h"
#include "websocket.h"
#include "param.h"

extern esp_mqtt_client_handle_t mqtt_client;
static const char *TAG = "TASKS";
static const char *TAG2 = "wifi_send";

// static adxl345_axes_t s_buffer1[BUF_SIZE];
// static adxl345_axes_t s_buffer2[BUF_SIZE];
static adxl345_buffer_t s_buffer1;
static adxl345_buffer_t s_buffer2;

QueueHandle_t s_free_queue = NULL; // очередь свободных буферов
QueueHandle_t s_data_queue = NULL; // очередь заполненных буферов
QueueHandle_t s_spectrum_queue = NULL;

static void timer_callback(void *arg)
{
    TaskHandle_t task = (TaskHandle_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(task, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void buffers_init(void)
{
    s_free_queue = xQueueCreate(2, sizeof(adxl345_buffer_t *));
    s_data_queue = xQueueCreate(2, sizeof(adxl345_buffer_t *));
    s_spectrum_queue = xQueueCreate(2, sizeof(adxl345_buffer_t *));
    if (s_free_queue == NULL || s_data_queue == NULL || s_spectrum_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create queues");
        return;
    }

    adxl345_buffer_t *buf_ptrs[2] = {&s_buffer1, &s_buffer2};
    for (int i = 0; i < 2; i++)
    {
        if (xQueueSend(s_free_queue, &buf_ptrs[i], 0) != pdTRUE)
        {
            ESP_LOGE(TAG, "Failed to send buffer %d to free_queue", i);
        }
        else
        {
            ESP_LOGI(TAG, "Buffer %d sent to free_queue: %p", i, buf_ptrs[i]);
        }
    }
}

void adxl345_read_axes(void *pvParameters)
{
    uint8_t *rx_buffer = heap_caps_malloc(7, MALLOC_CAP_DMA);
    uint8_t *tx_buffer = heap_caps_malloc(7, MALLOC_CAP_DMA);
    if (rx_buffer == NULL || tx_buffer == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate DMA memory");
        vTaskDelete(NULL);
        return;
    }
    tx_buffer[0] = 0xC0 | 0x32;
    for (int i = 1; i < 7; i++)
        tx_buffer[i] = 0xFF;

    spi_transaction_t t = {
        .length = 8 * 7,
        .tx_buffer = tx_buffer,
        .rx_buffer = rx_buffer};

    // Таймер [хуйня]
    esp_timer_handle_t timer;
    esp_timer_create_args_t timer_args = {
        .callback = &timer_callback,
        .arg = xTaskGetCurrentTaskHandle(),
        .name = "adxl_timer"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 313));

    // первый свободный буфер из внутренней очереди
    adxl345_buffer_t *current_buf;
    if (xQueueReceive(s_free_queue, &current_buf, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to get free buffer");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Initial buffer: %p", current_buf);
    if (current_buf == NULL)
    {
        ESP_LOGE(TAG, "Buffer is NULL!");
        vTaskDelete(NULL);
        return;
    }

    int idx = 0;
    while (1)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        spi_device_transmit(spi_adxl, &t); // spi_adxl из adxl345.h (extern)

        int16_t raw_x = (int16_t)(rx_buffer[1] | (rx_buffer[2] << 8));
        int16_t raw_y = (int16_t)(rx_buffer[3] | (rx_buffer[4] << 8));
        int16_t raw_z = (int16_t)(rx_buffer[5] | (rx_buffer[6] << 8));

        // current_buf[idx].x = raw_x * 0.004f;
        // current_buf[idx].y = raw_y * 0.004f;
        // current_buf[idx].z = raw_z * 0.004f;
        float ax = raw_x * 0.004f;
        float ay = raw_y * 0.004f;
        float az = raw_z * 0.004f;

        float module_g = sqrtf(ax * ax + ay * ay + az * az);
        // current_buf[idx].z = sqrt((raw_z * 0.004f)*(raw_z * 0.004f)+(raw_y * 0.004f)*(raw_y * 0.004f)+(raw_x * 0.004f)*(raw_x * 0.004f));
        current_buf -> accel[idx]=module_g;
        idx++;

        if (idx >= BUF_SIZE)
        {
            // Отправляем в очередь данных
            if (xQueueSend(s_data_queue, &current_buf, 0) != pdTRUE)
            {
                ESP_LOGE(TAG, "Data queue full! Data lost?");
            }
            // новый свободный буфер
            if (xQueueReceive(s_free_queue, &current_buf, portMAX_DELAY) != pdTRUE)
            {
                ESP_LOGE(TAG, "Failed to get free buffer");
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

void RMS_task(void *pvParameters)
{
    adxl345_buffer_t *buf;
    while (1)
    {
        if (xQueueReceive(s_data_queue, &buf, portMAX_DELAY) == pdTRUE)
        {
            if (buf == NULL)
            {
                ESP_LOGE(TAG, "Received NULL buffer from data_queue");
                continue;
            }

            float sum_sq = 0.0;
            for (int i = 0; i < BUF_SIZE; i++)
            {
                sum_sq += buf->accel[i] * buf->accel[i];
            }
            float rms = sqrtf(sum_sq / BUF_SIZE);
            ESP_LOGI(TAG, "Buffer %p RMS = %.4f g", buf, rms);
            if (xQueueSend(s_free_queue, &buf, 0) != pdTRUE)
            {
                ESP_LOGE(TAG, "Failed to return buffer to free_queue");
            }
        }
    }
}

void wifi_websocket_send_task(void *pvParameters)
{
    adxl345_buffer_t *buf;
    const size_t packet_size = BUF_SIZE * sizeof(float);

    while (1)
    {
        if (xQueueReceive(s_data_queue, &buf, portMAX_DELAY) == pdTRUE)
        {
            if (buf == NULL)
            {
                ESP_LOGE(TAG, "Received NULL buffer from data_queue");
                continue;
            }

            esp_websocket_client_handle_t ws_client = websocket_app_get_client();
            if (ws_client == NULL || !esp_websocket_client_is_connected(ws_client))
            {
                ESP_LOGW(TAG2, "WebSocket client not ready, dropping packet");
            }
            else
            {

                int bytes_sent = esp_websocket_client_send_bin(ws_client,
                                                               (const char *)buf->accel,
                                                               packet_size,
                                                               pdMS_TO_TICKS(100));

                if (bytes_sent > 0)
                {
                    ESP_LOGI(TAG2, "Sent %d bytes via WebSocket", bytes_sent);
                }
                else if (bytes_sent == -1)
                {
                    ESP_LOGE(TAG2, "WebSocket send timeout or error");
                }
            }

            // возращение буфера в свободную очередь
            if (xQueueSend(s_free_queue, &buf, 0) != pdTRUE)
            {
                ESP_LOGE(TAG2, "FAILED TO RETURN BUF TO FREE QUEUE");
            }
        }
    }
}

void wifi_mqtt_send_task(void *pvParameters)
{
    adxl345_buffer_t *buf;
    // const size_t packet_size = BUF_SIZE * 3 * sizeof(float);
    const size_t packet_size = BUF_SIZE * sizeof(float);

    while (1)
    {
        if (xQueueReceive(s_data_queue, &buf, portMAX_DELAY) == pdTRUE)
        {
            if (buf == NULL)
            {
                ESP_LOGE(TAG, "Received NULL buffer from data_queue");
                continue;
            }

            esp_mqtt_client_handle_t mqtt_client = mqtt_app_get_client();
            if (mqtt_client == NULL)
            {
                ESP_LOGW(TAG2, "MQTT client not ready, dropping packet");
            }
            else
            {
                int message_id = esp_mqtt_client_enqueue(mqtt_client, "/vibration/data", (const char *)buf->accel, packet_size, 0, 0, true);
                // int message_id = esp_mqtt_client_publish(mqtt_client, "/vibration/data", (const char *)packet, packet_size, 0, 0);
                if (message_id == -1)
                {
                    ESP_LOGE(TAG2, "msg_id = -1 FAIL");
                    int outbox_size = esp_mqtt_client_get_outbox_size(mqtt_client);
                    ESP_LOGI(TAG2, "Current outbox size: %d bytes", outbox_size);
                }
                else if (message_id == -2)
                {
                    ESP_LOGE(TAG2, "msg_id = -2 FAIL! FULL QUEUE");
                }
                else
                {
                    ESP_LOGI(TAG2, "queue succesfull!");
                    ESP_LOGI(TAG2, "Buffer %p send succesfull", buf);
                    int outbox_size = esp_mqtt_client_get_outbox_size(mqtt_client);
                    ESP_LOGI(TAG2, "Current outbox size: %d bytes", outbox_size);
                }
            }
        }
        if (xQueueSend(s_free_queue, &buf, 0) != pdTRUE)
        {
            ESP_LOGE(TAG2, "FAILED TO RETURN BUF TO FREE QUEUE");
        }
    }
}

void test(void *pvParameters)
{
    static const char test_data[1600] = {0};
    const size_t test_len = sizeof(test_data) - 1;
    TickType_t last_send_time = xTaskGetTickCount();
    const TickType_t interval = pdMS_TO_TICKS(500);
    while (1)
    {
        vTaskDelayUntil(&last_send_time, interval);

        esp_websocket_client_handle_t ws_client = websocket_app_get_client();
        if (ws_client == NULL || !esp_websocket_client_is_connected(ws_client))
        {
            ESP_LOGW("test_ws", "WebSocket client not ready");
            continue;
        }

        int bytes_sent = esp_websocket_client_send_bin(ws_client,
                                                       test_data,
                                                       test_len,
                                                       pdMS_TO_TICKS(100));
        if (bytes_sent > 0)
        {
            ESP_LOGI("test_ws", "Sent test data (%d bytes)", bytes_sent);
        }
        else
        {
            ESP_LOGE("test_ws", "Send failed: %d", bytes_sent);
        }
    }
}

void test_mqtt(void *pvParameters)
{
    static const char test_data[16000] = {0};
    const size_t test_len = sizeof(test_data);
    TickType_t last_send_time = xTaskGetTickCount();
    const TickType_t interval = pdMS_TO_TICKS(2000);

    while (1)
    {
        vTaskDelayUntil(&last_send_time, interval);

        esp_mqtt_client_handle_t mqtt_client = mqtt_app_get_client();
        if (mqtt_client == NULL)
        {
            ESP_LOGW("test_mqtt", "MQTT client not ready");
            continue;
        }

        int msg_id = esp_mqtt_client_enqueue(mqtt_client,
                                             "/vibration/data",
                                             test_data,
                                             test_len,
                                             0, // QoS
                                             0,
                                             true);

        if (msg_id >= 0)
        {
            ESP_LOGI("test_mqtt", "MQTT enqueued test data (msg_id=%d)", msg_id);
        }
        else if (msg_id == -1)
        {
            ESP_LOGE("test_mqtt", "MQTT enqueue failed (general error)");
        }
        else if (msg_id == -2)
        {
            ESP_LOGE("test_mqtt", "MQTT enqueue failed (outbox full)");
        }
    }
}



void websocket_send_spectrum_task(void *pvParameters)
{
    adxl345_buffer_t *buf;
    const size_t packet_size = sizeof(adxl345_buffer_t);

    while (1) {
        if (xQueueReceive(s_spectrum_queue, &buf, portMAX_DELAY) == pdTRUE) {
            if (!buf) continue;

            esp_websocket_client_handle_t ws = websocket_app_get_client();
            if (ws && esp_websocket_client_is_connected(ws)) {
                int sent = esp_websocket_client_send_bin(ws,
                                                         (const char*)buf,
                                                         packet_size,
                                                         pdMS_TO_TICKS(100));
                if (sent > 0) {
                    ESP_LOGI(TAG2, "Sent spectrum packet, size=%d", sent);
                } else {
                    ESP_LOGE(TAG2, "WebSocket send failed");
                }
            } else {
                ESP_LOGW(TAG2, "WebSocket not ready, dropping packet");
            }
            if (xQueueSend(s_free_queue, &buf, 0) != pdTRUE) {
                ESP_LOGE(TAG2, "Failed to return buffer to free_queue");
            }
        }
    }
}