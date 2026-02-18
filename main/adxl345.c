#include "adxl345.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

static const char *TAG = "ADXL345_SPI";

#define ADXL345_CS_GPIO     GPIO_NUM_5
#define ADXL345_SPI_HOST    SPI2_HOST
#define ADXL345_INT1_GPIO   GPIO_NUM_4

spi_device_handle_t spi_adxl = NULL;

uint8_t adxl345_read_byte(uint8_t reg_addr)
{
    uint8_t tx_data[2] = {0x80 | reg_addr, 0xFF};
    uint8_t rx_data[2];
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data};
    spi_device_transmit(spi_adxl, &t);
    return rx_data[1];
}

void adxl345_write_byte(uint8_t reg_addr, uint8_t data)
{
    uint8_t tx_data[2] = {reg_addr & 0x3F, data};
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx_data};
    spi_device_transmit(spi_adxl, &t);
}

void adxl345_init_spi(void)
{
    spi_bus_config_t buscfg = {
        .miso_io_num = GPIO_NUM_19,
        .mosi_io_num = GPIO_NUM_23,
        .sclk_io_num = GPIO_NUM_18,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 10};

    spi_device_interface_config_t devcfg = {
        .mode = 3,
        .clock_speed_hz = 1.5e6,
        .spics_io_num = ADXL345_CS_GPIO,
        .queue_size = 7,
    };

    ESP_ERROR_CHECK(spi_bus_initialize(ADXL345_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(ADXL345_SPI_HOST, &devcfg, &spi_adxl));
    ESP_LOGI(TAG, "SPI initialized");
}


void adxl345_configure(void)
{
    adxl345_write_byte(0x2D, 0x08); // POWER_CTL: включить измерения
    adxl345_write_byte(0x31, 0x09); // DATA_FORMAT: +-4g, FULL_RES=1
    adxl345_write_byte(0x2C, 0x0F); // DATA_RATE = 3200 Гц
}


void adxl345_force_4wire_spi(void)
{
    uint8_t data_format = adxl345_read_byte(0x31);
    data_format &= ~0x40;
    adxl345_write_byte(0x31, data_format);
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "DATA_FORMAT: 0x%02X", adxl345_read_byte(0x31));
}

bool adxl345_check_presence(void) {
    uint8_t devid = adxl345_read_byte(0x00);
    if (devid != 0xE5) {
        ESP_LOGE(TAG, "ADXL345 not found! ID=0x%02X", devid);
        return false;
    }
    ESP_LOGI(TAG, "ADXL345 found! ID=0x%02X", devid);
    return true;
}

