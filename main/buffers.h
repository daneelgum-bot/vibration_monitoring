#ifndef BUFFERS_H
#define BUFFERS_H

#include "adxl345.h"   
//#include "param.h"
//#define BUF_SIZE 1600

typedef struct {
    float x;
    float y;
    float z;
} adxl345_axes_t;

typedef struct {
    //float x[BUF_SIZE];
    //float y[BUF_SIZE];
    //float z[BUF_SIZE];
    float sensor_id;
    float rms_speed;
    float accel[BUF_SIZE];
    float spectrum[BUF_SIZE/2];
} adxl345_buffer_t;



extern QueueHandle_t s_free_queue;
extern QueueHandle_t s_data_queue;
extern QueueHandle_t s_spectrum_queue;

void adxl345_read_axes(void *pvParameters);
void wifi_send_task(void *pvParameters);
void RMS_task(void *pvParameters);
void buffers_init(void);
void wifi_websocket_send_task(void *pvParameters);
void websocket_send_spectrum_task(void *pvParameters);
void test(void *pvParameters);
void test_mqtt(void *pvParameters);
#endif
