#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef void *spi_device_handle_t;
#define SPI_TRANS_USE_RXDATA 1
typedef struct { unsigned flags; size_t length; size_t rxlength; const void *tx_buffer; uint8_t rx_data[4]; } spi_transaction_t;
esp_err_t spi_device_transmit(spi_device_handle_t spi, spi_transaction_t *trans);

#define SPI_DEVICE_HALFDUPLEX (1U << 4)
#define SPI_DEVICE_3WIRE (1U << 2)
#define SPI_DEVICE_NO_DUMMY (1U << 6)
typedef struct {
    int clock_speed_hz;
    int mode;
    int spics_io_num;
    int queue_size;
    unsigned flags;
} spi_device_interface_config_t;
