#pragma once

#include "driver/spi_master.h"
#include "pin_config.h"

// Sticky reads and writes the panel's SDA line on GPIO14 at 2 MHz.
// Sticky 通过 GPIO14 双向读写屏幕 SDA，时钟为 2 MHz。
inline spi_device_interface_config_t sticky_display_spi_device_config()
{
    spi_device_interface_config_t config = {};
    config.clock_speed_hz = 2 * 1000 * 1000;
    config.mode = 0;
    config.spics_io_num = PIN_EPD_CS;
    config.queue_size = 1;
    config.flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_3WIRE | SPI_DEVICE_NO_DUMMY;
    return config;
}
