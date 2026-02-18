#ifndef ADXL345_H
#define ADXL345_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/spi_master.h"

typedef struct {
    float x;
    float y;
    float z;
} adxl345_axes_t;

extern spi_device_handle_t spi_adxl;


void adxl345_init_spi(void);
bool adxl345_check_presence(void);   // новая, но удобная
void adxl345_configure(void);
void adxl345_force_4wire_spi(void);
uint8_t adxl345_read_byte(uint8_t reg_addr);
void adxl345_write_byte(uint8_t reg_addr, uint8_t data);

#endif