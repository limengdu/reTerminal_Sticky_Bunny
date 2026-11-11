#include <cassert>
#include <cstdio>

#include "sticky_display_spi_config.h"

int main()
{
    const spi_device_interface_config_t config = sticky_display_spi_device_config();
    assert(config.flags & SPI_DEVICE_HALFDUPLEX);
    assert(config.flags & SPI_DEVICE_3WIRE);
    assert(config.flags & SPI_DEVICE_NO_DUMMY);
    assert(config.clock_speed_hz == 2000000);
    assert(config.clock_speed_hz <= 2500000);
    assert(config.mode == 0 && config.spics_io_num == PIN_EPD_CS);
    assert(config.queue_size == 1);
    assert(PIN_EPD_MOSI == 14 && PIN_SD_MISO == 12 && PIN_EPD_CLK == 13);
    std::puts("[host-test] PASS sticky_display_spi_config_test");
}
