#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#ifndef GPIO_NUM_NC
#define GPIO_NUM_NC ((gpio_num_t)-1)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct seeed_epaper_panel_t *seeed_epaper_panel_handle_t;

typedef enum {
    SEEED_EPAPER_PANEL_UC8179 = 0,
    SEEED_EPAPER_PANEL_SSD1677,
    SEEED_EPAPER_PANEL_SSD2677,
} seeed_epaper_panel_model_t;

typedef enum {
    SEEED_EPAPER_REFRESH_FULL = 0,
    SEEED_EPAPER_REFRESH_PARTIAL,
    SEEED_EPAPER_REFRESH_GRAY4,
} seeed_epaper_refresh_mode_t;

typedef enum {
    SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB = 0,
    SEEED_EPAPER_PIXEL_FORMAT_GRAY4,
    SEEED_EPAPER_PIXEL_FORMAT_GRAY8,
    SEEED_EPAPER_PIXEL_FORMAT_RGB565,
} seeed_epaper_pixel_format_t;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} seeed_epaper_area_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t native_bpp;
    bool supports_partial;
    bool supports_gray4;
    bool supports_color;
} seeed_epaper_panel_info_t;

typedef struct {
    bool temperature_valid;
    bool temperature_external;
    uint8_t temperature_c;
    uint32_t latch_ms;
    uint32_t temperature_ms;
    uint32_t latch_apply_ms;
    uint32_t stream_ms;
    uint32_t stream_command_ms;
    uint32_t stream_encode_ms;
    uint32_t stream_spi_ms;
    uint32_t drive_ms;
    uint32_t state_sync_ms;
    uint32_t power_on_ms;
    uint32_t power_on_settle_ms;
    uint32_t refresh_ms;
    uint32_t power_off_ms;
    uint32_t power_off_settle_ms;
} seeed_epaper_refresh_timing_t;

typedef struct {
    // Shared SPI bus and device remain owned by the board layer.
    // 共用 SPI 总线及设备句柄仍由板级代码管理。
    spi_device_handle_t spi_handle;
    gpio_num_t pin_dc;
    gpio_num_t pin_cs;
    gpio_num_t pin_rst;
    gpio_num_t pin_busy;
    gpio_num_t pin_enable;
    uint32_t busy_timeout_ms;
    uint16_t reset_low_ms;
    uint16_t reset_high_ms;
    uint8_t busy_level;
    uint8_t enable_level;
    bool busy_level_valid;
    bool enable_level_valid;
    bool manual_cs;
    bool mirror_x;
} seeed_epaper_panel_config_t;

/**
 * Create a panel without taking ownership of the externally managed SPI handle.
 * Either SSD model probes register 0x70: 0x07 selects SSD2677, other successful
 * reads select legacy SSD1677. SPI errors are returned, not silently ignored.
 * Sticky uses HALFDUPLEX | 3WIRE at 2 MHz for bidirectional SDA on GPIO14.
 * Match SPI routing and read timing to the board; UC8179 is never auto-detected.
 * 自动识别两种 SSD 控制器；SPI 读取失败会返回错误。SPI 总线由调用方管理。
 */
esp_err_t seeed_epaper_new_panel(seeed_epaper_panel_model_t model,
                                 const seeed_epaper_panel_config_t *config,
                                 seeed_epaper_panel_handle_t *ret_panel);

esp_err_t seeed_epaper_panel_del(seeed_epaper_panel_handle_t panel);

esp_err_t seeed_epaper_panel_get_info(seeed_epaper_panel_handle_t panel,
                                      seeed_epaper_panel_info_t *ret_info);

esp_err_t seeed_epaper_panel_get_model(seeed_epaper_panel_handle_t panel,
                                       seeed_epaper_panel_model_t *ret_model);

esp_err_t seeed_epaper_panel_get_last_refresh_timing(seeed_epaper_panel_handle_t panel,
                                                      seeed_epaper_refresh_timing_t *ret_timing);

esp_err_t seeed_epaper_panel_set_external_temperature(seeed_epaper_panel_handle_t panel,
                                                      float temperature_c);

esp_err_t seeed_epaper_panel_clear_external_temperature(seeed_epaper_panel_handle_t panel);

esp_err_t seeed_epaper_panel_reset(seeed_epaper_panel_handle_t panel);

esp_err_t seeed_epaper_panel_wait_ready(seeed_epaper_panel_handle_t panel);

esp_err_t seeed_epaper_panel_read_register8(seeed_epaper_panel_handle_t panel,
                                            uint8_t command,
                                            uint8_t *ret_value);

esp_err_t seeed_epaper_panel_prepare(seeed_epaper_panel_handle_t panel,
                                     seeed_epaper_refresh_mode_t mode);

esp_err_t seeed_epaper_panel_write_bitmap(seeed_epaper_panel_handle_t panel,
                                          const seeed_epaper_area_t *area,
                                          const void *pixels,
                                          size_t src_stride_bytes,
                                          seeed_epaper_pixel_format_t input_format,
                                          seeed_epaper_refresh_mode_t mode);

esp_err_t seeed_epaper_panel_write_bitmap_diff(seeed_epaper_panel_handle_t panel,
                                               const seeed_epaper_area_t *area,
                                               const void *previous_pixels,
                                               const void *current_pixels,
                                               size_t src_stride_bytes,
                                               seeed_epaper_pixel_format_t input_format,
                                               seeed_epaper_refresh_mode_t mode);

// Write packed 2bpp pixels: 0=black, 1=dark gray, 2=light gray, 3=white.
// 写入打包的 2bpp 像素：0=黑、1=深灰、2=浅灰、3=白。
esp_err_t seeed_epaper_panel_write_bitmap_gray4(
    seeed_epaper_panel_handle_t panel,
    const seeed_epaper_area_t *area,
    const uint8_t *pixels,
    size_t src_stride_bytes);

esp_err_t seeed_epaper_panel_commit(seeed_epaper_panel_handle_t panel,
                                    const seeed_epaper_area_t *area,
                                    seeed_epaper_refresh_mode_t mode);

esp_err_t seeed_epaper_panel_refresh_area(seeed_epaper_panel_handle_t panel,
                                          const seeed_epaper_area_t *area,
                                          const void *pixels,
                                          size_t src_stride_bytes,
                                          seeed_epaper_pixel_format_t input_format,
                                          seeed_epaper_refresh_mode_t mode);

esp_err_t seeed_epaper_panel_clear(seeed_epaper_panel_handle_t panel,
                                   bool white,
                                   seeed_epaper_refresh_mode_t mode);

esp_err_t seeed_epaper_panel_sleep(seeed_epaper_panel_handle_t panel);

// Disable analog power while retaining RAM. prepare() resumes on the next update.
// 关闭模拟电源并保留显存；下一次 prepare() 自动恢复。
esp_err_t seeed_epaper_panel_standby(seeed_epaper_panel_handle_t panel);

esp_err_t seeed_epaper_panel_resume(seeed_epaper_panel_handle_t panel);

esp_err_t seeed_epaper_panel_wakeup(seeed_epaper_panel_handle_t panel,
                                    seeed_epaper_refresh_mode_t mode);

bool seeed_epaper_panel_busy_wait_active(void);

#ifdef __cplusplus
}
#endif
