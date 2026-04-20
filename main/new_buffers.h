#ifndef NEW_BUFFERS_H
#define _NEWBUFFERS_H
#include "param.h"
#include "new_adxl_two.h"

#define NUM_BUFFERS     6           // два датчика -> 4 буфера

typedef struct {
    uint32_t sensor_id;                
    float rms_speed;               
    float accel[BUF_SIZE];         
    float spectrum[BUF_SIZE/2];     
} adxl345_buffer_t; 

// Параметры, передаваемые в задачу чтения
typedef struct {
    adxl345_t *dev;
    uint8_t    sensor_id;
} read_task_param_t;

// Очереди
extern QueueHandle_t s_free_queue;
extern QueueHandle_t s_data_queue;
extern QueueHandle_t s_spectrum_queue;

// Функции
void buffers_init(void);
void adxl345_read_axes(void *pvParameters);
void fft_task(void *pvParameters);
void websocket_send_spectrum_task(void *pvParameters);

#endif