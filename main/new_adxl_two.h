#ifndef NEW_ADXL_TWO_H
#define NEW_ADXL_TWO_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/spi_master.h"

typedef struct 
{
    spi_device_handle_t handle;
    spi_host_device_t host;
    int cs_pin;
} adxl345_t;

esp_err_t adxl345_bus_init(spi_host_device_t host, int miso, int mosi, int sclk);

esp_err_t adxl345_init(adxl345_t *dev, spi_host_device_t host, int cs_pin);

uint8_t adxl345_read_byte(adxl345_t *dev, uint8_t reg);
void adxl345_write_byte(adxl345_t *dev, uint8_t reg, uint8_t data);

void adxl345_configure(adxl345_t *dev);
void adxl345_force_4wire_spi(adxl345_t *dev);
bool adxl345_check_presence(adxl345_t *dev);

#endif