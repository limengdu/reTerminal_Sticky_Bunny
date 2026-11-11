# Sticky screen compatibility

## Scope

Version 0.1.3 integrates the supplied Seeed ePaper controller reference into the
existing display API. It supports SSD1677 and SSD2677 without separate images.
UC8179 remains an explicitly selected component driver, not a Sticky detection
candidate.

- Probe: hardware reset, command `0x70`, one-byte receive.
- ID `0x07`: SSD2677, BUSY low, 30 s timeout, 20/10 ms reset.
- Other successful reads: SSD1677, BUSY high, 8 s timeout, 10/10 ms reset.
- SPI errors: initialization fails. A floating read line that returns `0x00` or
  `0xFF` still selects the legacy driver and emits a warning; physical validation
  must verify the detected controller and successful refresh.
- SPI2 retains MOSI 14, MISO 12 and CLK 13. The display device uses ESP-IDF's
  `HALFDUPLEX | 3WIRE | NO_DUMMY`: GPIO14 carries both panel writes and readback;
  GPIO12 remains the MicroSD return line. The panel retains separate DC and
  eight-bit commands.
- The display device runs at 2 MHz for reads and writes. SSD2677 specifies a
  minimum 400 ns read cycle (2.5 MHz maximum) and 180 ns high/low periods.
  A 96,000-byte transfer therefore takes about 384 ms of wire time, approximately
  307 ms more than at 10 MHz. The SD device retains its own clock configuration.
- SSD2677 stages current and previous 48,000-byte mono frames and sends a
  96,000-byte two-bit stream. Four-level grayscale needs another 96,000-byte
  buffer. Allocation prefers PSRAM.
- Partial refresh uses a complete comparison frame even for a small changed
  region. A successful commit updates the previous frame only after refresh.
- SSD1677 retains its existing software mirror and RAM scan order. A gray4
  refresh invalidates its mono baseline so the next partial request becomes a
  clean full refresh.
- Standby disables analog power without discarding the comparison state. The
  next refresh resumes automatically. Deep sleep invalidates the baseline.

The SSD2677 register `0x61` values `03 20 02 A8` are retained verbatim from the
supplied reference. The logical image is 800 x 480; the controller scan register
must not be inferred from the framebuffer height without vendor confirmation.

## Readback wiring and timing references

- [Seeed Sticky schematic](https://files.seeedstudio.com/wiki/reterminal_sticky/res/reTerminal_Sticky_Schematic_diagram_260609.pdf),
  sheets 5 and 10: GPIO14/EP_SDI connects directly to panel SDA; GPIO12 serves SD.
- [Seeed Sticky hardware overview](https://www.seeedstudio.com/sticky/docs/en/device-guide/hardware-overview/):
  display SDI is GPIO14; MicroSD MISO is GPIO12.
- [SSD2677 datasheet](https://files.waveshare.com/wiki/common/SSD2677_1.0.pdf),
  sections 5, 6.1.2 and 12.1: bidirectional SDA, separate DC, and read timing.
- [ESP-IDF SPI device options](https://docs.espressif.com/projects/esp-idf/en/v5.4.1/esp32s3/api-reference/peripherals/spi_master.html):
  `SPI_DEVICE_3WIRE` uses MOSI for transmission and reception.

The board configuration test compiles the same configuration helper used by the
firmware and checks bidirectional routing, the 2 MHz clock and shared SD pins.
The controller tests separately inject SPI responses to verify driver behavior.
Both types of checks require physical readback and image validation afterward.

## Automated checks

```bash
./tools/run_host_tests.sh
./tools/run_epaper_tests.sh
python3 tools/check_markdown_links.py
pio run -e sticky-debug -e sticky-power-test -e sticky-release
```

The mocked driver tests compile the actual common core and drivers and observe
SPI traffic. They do not prove electrical readback, optical waveforms or current
draw on a real display.

## Required hardware matrix

Run this checklist on **both SSD1677 and SSD2677**, once on USB and once on
battery. Record serial logs, firmware commit/profile and any visual artifacts.

1. Cold boot: verify `panel probe` and `display=panel_ready` select the correct
   controller, with no SPI/BUSY errors or blank screen.
2. Open launcher and each app; verify orientation and touch alignment in
   portrait and landscape. Check asymmetric labels at all four corners.
3. Display the pet/home grayscale artwork and verify black, dark gray, light
   gray and white are distinct and in the expected order.
4. Exercise Pomodoro digits, pet animation and at least 20 partial transitions.
   Verify unchanged regions remain stable and periodic full cleanup works.
5. Alternate gray4, monochrome full and partial refreshes. Verify the first
   refresh after sleep establishes a valid baseline with no stale-image trails.
6. Hold both side keys to sleep, then wake. Repeat sleep/wake and cold power
   cycling, including a gray4 page as the first screen after wake.
7. Confirm temperature read succeeds on SSD2677 and waveform bucket logs are
   sensible with timing diagnostics enabled. Do not heat or chill hardware
   outside the vendor operating range.
8. Measure idle current after full and partial updates. Confirm standby stops
   analog power and that RTC/touch wake and the shared SD bus still behave.

Hardware validation is still required before describing this release as tested
on both physical revisions. No screen hardware is connected to the automated
build/test environment.
