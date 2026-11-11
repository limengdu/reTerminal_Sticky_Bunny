#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef void *spi_device_handle_t;
#define SPI_TRANS_USE_RXDATA 1
typedef struct { unsigned flags; size_t length; size_t rxlength; const void *tx_buffer; uint8_t rx_data[4]; } spi_transaction_t;
esp_err_t spi_device_transmit(spi_device_handle_t spi, spi_transaction_t *trans);
