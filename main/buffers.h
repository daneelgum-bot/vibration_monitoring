#ifndef BUFFERS_H
#define BUFFERS_H

#include "adxl345.h"   

#define BUF_SIZE 3200


void adxl345_read_axes(void *pvParameters);
void wifi_send_task(void *pvParameters);
void RMS_task(void *pvParameters);
void buffers_init(void);
void wifi_websocket_send_task(void *pvParameters);
void test(void *pvParameters);
void test_mqtt(void *pvParameters);
#endif
