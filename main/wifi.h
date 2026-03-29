#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"
#include <stdbool.h>
#include "freertos/FreeRTOS.h"   
#include "freertos/task.h"
       
esp_err_t wifi_init_sta(void);

bool wifi_is_connected(void);

// Ожидание подключения (блокирующая функция с таймаутом)
esp_err_t wifi_wait_connected(TickType_t ticks_to_wait);

#endif