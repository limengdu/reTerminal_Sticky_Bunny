# SEEED_EPAPER

ESP-IDF panel component with a shared framebuffer/SPI/session core and separate
UC8179, SSD1677 and SSD2677 drivers. SPI devices are created by the board layer.

## Controller selection

`seeed_epaper_new_panel()` with either SSD model probes register `0x70`.
`0x07` selects SSD2677; any other successful read selects SSD1677. Failed SPI
reads return an error. `seeed_epaper_panel_get_model()` returns the actual
selection. UC8179 bypasses probing.

The SPI device must support receive-only, half-duplex transactions. Sticky uses
`SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_3WIRE | SPI_DEVICE_NO_DUMMY` at 2 MHz:
commands and register reads share the panel SDA line on GPIO14. GPIO12 remains
the MicroSD return line on the shared bus. ESP-IDF's `3WIRE` option selects the
bidirectional data line; the panel still uses its separate DC pin and eight-bit
commands. Automatic selection sets the model-specific BUSY polarity, reset
delays and timeout. Values `0x00` and `0xFF` retain the vendor's legacy selection
and emit a readback warning for hardware diagnosis.

## Refresh paths

- Full monochrome: `prepare` + `write_bitmap` + `commit`, or `refresh_area`.
- Partial: uses previous/current images and requires a full baseline first.
- Four-level grayscale: `write_bitmap_gray4` + `commit(GRAY4)`.
- SSD2677 retains full current/previous mono framebuffers and a packed gray4
  buffer. At commit it streams 96,000 bytes through command `0x10`; a small dirty
  rectangle does not reduce that transfer size.
- `standby` disables analog power while preserving RAM; `prepare` resumes the
  panel for the next update. Call standby when an application is idle.
- `sleep` enters deep sleep and invalidates the partial baseline.

Sticky Bunny calls standby after each successful refresh. Controller detection
is always logged; detailed timing is gated by `STICKY_LOG_DISPLAY_TIMING_ENABLED`.

The driver command tables and waveforms originate from the supplied Seeed
SSD1677/SSD2677 compatibility reference. The integration adds cleanup on failed
creation, mirrored gray4 comparison-state correction, legacy SSD1677 software mirroring
and hardware reset, and a fresh mono baseline after SSD1677 gray4.

See [hardware validation](../../docs/screen_compatibility.md).
