#ifndef BUFFERS_H
#define BUFFERS_H

#include "adxl345.h"   

#define BUF_SIZE 1600


void adxl345_read_axes(void *pvParameters);
void wifi_send_task(void *pvParameters);

void buffers_init(void);

#endif