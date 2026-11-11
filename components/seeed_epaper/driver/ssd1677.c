#include "epaper_panel_priv.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_debug_log.h"

// SSD1677 command sequencing for the 800x480 reTerminal Sticky panel.
// reTerminal Sticky 800x480屏幕使用的SSD1677命令时序实现。
static const char *TAG = "ssd1677";

static uint64_t driver_log_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}

static const char *driver_mode_name(seeed_epaper_refresh_mode_t mode)
{
    switch (mode) {
    case SEEED_EPAPER_REFRESH_PARTIAL:
        return "partial";
    case SEEED_EPAPER_REFRESH_GRAY4:
        return "gray4";
    case SEEED_EPAPER_REFRESH_FULL:
    default:
        return "full";
    }
}

static esp_err_t write_reg8(seeed_epaper_panel_t *panel, uint8_t command, uint8_t value)
{
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, command), "ssd1677", "write command failed");
    return seeed_epaper_panel_write_data(panel, &value, 1);
}

static esp_err_t ssd1677_init_base(seeed_epaper_panel_t *panel, seeed_epaper_refresh_mode_t mode)
{
    // Configure power, scan direction, RAM bounds and cursor before image transfer.
    // 传输图像前依次配置电源、扫描方向、显存边界和写入游标。
    const uint64_t start_ms = driver_log_ms();
    APP_EPAPER_DEBUG_LOGI(TAG,
             "init_base start: mode=%s size=%ux%u",
             driver_mode_name(mode),
             (unsigned)panel->info.width,
             (unsigned)panel->info.height);
    const uint8_t softstart[] = {0xAE, 0xC7, 0xC3, 0xC0, 0x80};
    const uint8_t driver_output[] = {
        (uint8_t)((panel->info.height - 1U) & 0xFF),
        (uint8_t)((panel->info.height - 1U) >> 8),
        0x02,
    };
    const uint8_t ram_x[] = {
        0x00,
        0x00,
        (uint8_t)((panel->info.width - 1U) & 0xFF),
        (uint8_t)((panel->info.width - 1U) >> 8),
    };
    const uint8_t ram_y[] = {
        0x00,
        0x00,
        (uint8_t)((panel->info.height - 1U) & 0xFF),
        (uint8_t)((panel->info.height - 1U) >> 8),
    };
    const uint8_t zero2[] = {0x00, 0x00};
    uint8_t border_waveform;
    if (mode == SEEED_EPAPER_REFRESH_GRAY4) {
        border_waveform = 0x00;
    } else if (mode == SEEED_EPAPER_REFRESH_PARTIAL) {
        border_waveform = 0x80;
    } else {
        border_waveform = 0x01;
    }

    ESP_RETURN_ON_ERROR(seeed_epaper_panel_reset(panel), "ssd1677", "reset failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_wait_ready(panel), "ssd1677", "wait ready failed");
    if (mode == SEEED_EPAPER_REFRESH_FULL) {
        ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x12), "ssd1677", "write command failed");
        ESP_RETURN_ON_ERROR(seeed_epaper_panel_wait_ready(panel), "ssd1677", "wait ready failed");
    }
    ESP_RETURN_ON_ERROR(write_reg8(panel, 0x18, 0x80), "ssd1677", "write reg failed");
    ESP_RETURN_ON_ERROR(write_reg8(panel, 0x3C, border_waveform), "ssd1677", "write reg failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x0C), "ssd1677", "write command failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_data(panel, softstart, sizeof(softstart)), "ssd1677", "write softstart failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x01), "ssd1677", "write command failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_data(panel, driver_output, sizeof(driver_output)), "ssd1677", "write driver output failed");
    ESP_RETURN_ON_ERROR(write_reg8(panel, 0x11, 0x03), "ssd1677", "write reg failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x44), "ssd1677", "write command failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_data(panel, ram_x, sizeof(ram_x)), "ssd1677", "write ram x failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x45), "ssd1677", "write command failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_data(panel, ram_y, sizeof(ram_y)), "ssd1677", "write ram y failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x4E), "ssd1677", "write command failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_data(panel, zero2, sizeof(zero2)), "ssd1677", "write counter x failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x4F), "ssd1677", "write command failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_data(panel, zero2, sizeof(zero2)), "ssd1677", "write counter y failed");
    esp_err_t err = seeed_epaper_panel_wait_ready(panel);
    APP_EPAPER_DEBUG_LOGI(TAG,
             "init_base done: mode=%s elapsed=%llums err=%s",
             driver_mode_name(mode),
             (unsigned long long)(driver_log_ms() - start_ms),
             esp_err_to_name(err));
    return err;
}

static esp_err_t ssd1677_init_mode(seeed_epaper_panel_t *panel, seeed_epaper_refresh_mode_t mode)
{
    APP_EPAPER_DEBUG_LOGI(TAG, "init_mode: %s", driver_mode_name(mode));
    ESP_RETURN_ON_ERROR(ssd1677_init_base(panel, mode), "ssd1677", "base init failed");
    return ESP_OK;
}

static esp_err_t ssd1677_set_window(seeed_epaper_panel_t *panel, const seeed_epaper_area_t *area)
{
    APP_EPAPER_DEBUG_LOGI(TAG,
             "set_window: x=%u y=%u w=%u h=%u",
             (unsigned)area->x,
             (unsigned)area->y,
             (unsigned)area->width,
             (unsigned)area->height);
    const uint16_t x_end = (uint16_t)(area->x + area->width - 1U);
    const uint16_t y_end = (uint16_t)(area->y + area->height - 1U);
    const uint8_t ram_x[] = {
        (uint8_t)(area->x & 0xFF),
        (uint8_t)(area->x >> 8),
        (uint8_t)(x_end & 0xFF),
        (uint8_t)(x_end >> 8),
    };
    const uint8_t ram_y[] = {
        (uint8_t)(area->y & 0xFF),
        (uint8_t)(area->y >> 8),
        (uint8_t)(y_end & 0xFF),
        (uint8_t)(y_end >> 8),
    };
    const uint8_t cursor_x[] = {
        (uint8_t)(area->x & 0xFF),
        (uint8_t)(area->x >> 8),
    };
    const uint8_t cursor_y[] = {
        (uint8_t)(area->y & 0xFF),
        (uint8_t)(area->y >> 8),
    };

    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x44), "ssd1677", "write command failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_data(panel, ram_x, sizeof(ram_x)), "ssd1677", "write ram x failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x45), "ssd1677", "write command failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_data(panel, ram_y, sizeof(ram_y)), "ssd1677", "write ram y failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x4E), "ssd1677", "write command failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_data(panel, cursor_x, sizeof(cursor_x)), "ssd1677", "write cursor x failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x4F), "ssd1677", "write command failed");
    return seeed_epaper_panel_write_data(panel, cursor_y, sizeof(cursor_y));
}

static esp_err_t ssd1677_write_image(seeed_epaper_panel_t *panel, const uint8_t *buffer, size_t len, bool previous_plane)
{
    APP_EPAPER_DEBUG_LOGI(TAG,
             "write_image: plane=%s len=%u",
             previous_plane ? "previous" : "current",
             (unsigned)len);
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, previous_plane ? 0x26 : 0x24), "ssd1677", "write command failed");
    return seeed_epaper_panel_write_data(panel, buffer, len);
}

static esp_err_t ssd1677_refresh(seeed_epaper_panel_t *panel, seeed_epaper_refresh_mode_t mode)
{
    const uint64_t start_ms = driver_log_ms();
    uint64_t drive_start_ms;
    panel->last_refresh_timing = (seeed_epaper_refresh_timing_t){0};
    APP_EPAPER_DEBUG_LOGI(TAG, "refresh start: mode=%s", driver_mode_name(mode));

    // Gray refresh uses the controller's OTP temperature compensation value.
    // 四级灰度刷新前写入控制器OTP波形要求的温度补偿值。
    if (mode == SEEED_EPAPER_REFRESH_GRAY4) {
        const uint8_t temp_comp[] = {0x67, 0x00};
        ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x1A),
                            "ssd1677", "write temp command failed");
        ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_data(panel, temp_comp, sizeof(temp_comp)),
                            "ssd1677", "write temp data failed");
    }

    uint8_t value;
    if (mode == SEEED_EPAPER_REFRESH_GRAY4) {
        value = 0xD7;
    } else if (mode == SEEED_EPAPER_REFRESH_PARTIAL) {
        ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x21),
                            "ssd1677",
                            "write display update control failed");
        const uint8_t update_control[] = {0x00, 0x00};
        ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_data(panel, update_control, sizeof(update_control)),
                            "ssd1677",
                            "write display update control data failed");
        value = panel->partial_after_standby ? 0xFF : 0xFC;
    } else {
        /* Keep the analog supply and internal clock enabled after full refresh. */
        value = 0xF4;
    }
    ESP_RETURN_ON_ERROR(write_reg8(panel, 0x22, value), "ssd1677", "write reg failed");
    panel->last_refresh_timing.latch_ms = (uint32_t)(driver_log_ms() - start_ms);
    drive_start_ms = driver_log_ms();
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x20), "ssd1677", "write command failed");
    esp_err_t err = seeed_epaper_panel_wait_ready(panel);
    panel->last_refresh_timing.drive_ms = (uint32_t)(driver_log_ms() - drive_start_ms);
    APP_EPAPER_DEBUG_LOGI(TAG,
             "refresh done: mode=%s elapsed=%llums err=%s",
             driver_mode_name(mode),
             (unsigned long long)(driver_log_ms() - start_ms),
             esp_err_to_name(err));
    return err;
}

static esp_err_t ssd1677_sleep(seeed_epaper_panel_t *panel)
{
    const uint64_t start_ms = driver_log_ms();
    APP_EPAPER_DEBUG_LOGI(TAG, "sleep start");
    ESP_RETURN_ON_ERROR(write_reg8(panel, 0x10, 0x03), "ssd1677", "write reg failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    APP_EPAPER_DEBUG_LOGI(TAG,
             "sleep done: elapsed=%llums",
             (unsigned long long)(driver_log_ms() - start_ms));
    return ESP_OK;
}

static esp_err_t ssd1677_standby(seeed_epaper_panel_t *panel)
{
    const uint64_t start_ms = driver_log_ms();
    APP_EPAPER_DEBUG_LOGI(TAG, "standby start");
    ESP_RETURN_ON_ERROR(write_reg8(panel, 0x22, 0x03), "ssd1677", "write reg failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x20), "ssd1677", "write command failed");
    esp_err_t err = seeed_epaper_panel_wait_ready(panel);
    APP_EPAPER_DEBUG_LOGI(TAG,
             "standby done: elapsed=%llums err=%s",
             (unsigned long long)(driver_log_ms() - start_ms),
             esp_err_to_name(err));
    return err;
}

static esp_err_t ssd1677_resume(seeed_epaper_panel_t *panel)
{
    const uint64_t start_ms = driver_log_ms();
    APP_EPAPER_DEBUG_LOGI(TAG, "resume start");
    ESP_RETURN_ON_ERROR(write_reg8(panel, 0x22, 0xC0), "ssd1677", "write reg failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x20), "ssd1677", "write command failed");
    esp_err_t err = seeed_epaper_panel_wait_ready(panel);
    APP_EPAPER_DEBUG_LOGI(TAG,
             "resume done: elapsed=%llums err=%s",
             (unsigned long long)(driver_log_ms() - start_ms),
             esp_err_to_name(err));
    return err;
}

const seeed_epaper_driver_t *seeed_epaper_ssd1677_driver(void)
{
    static const seeed_epaper_driver_t driver = {
        .name = "SSD1677",
        .info = {
            .width = 800,
            .height = 480,
            .native_bpp = 1,
            .supports_partial = true,
            .supports_gray4 = true,
            .supports_color = false,
        },
        .init_mode = ssd1677_init_mode,
        .set_window = ssd1677_set_window,
        .write_image = ssd1677_write_image,
        .refresh = ssd1677_refresh,
        .sleep = ssd1677_sleep,
        .standby = ssd1677_standby,
        .resume = ssd1677_resume,
    };
    return &driver;
}
