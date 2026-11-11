#include "epaper_panel_priv.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_debug_log.h"

#define SSD2677_WIDTH 800U
#define SSD2677_HEIGHT 480U
#define SSD2677_1BPP_STRIDE (SSD2677_WIDTH / 8U)
#define SSD2677_1BPP_LEN (SSD2677_1BPP_STRIDE * SSD2677_HEIGHT)
#define SSD2677_2BPP_STRIDE (SSD2677_WIDTH / 4U)
#define SSD2677_2BPP_LEN (SSD2677_2BPP_STRIDE * SSD2677_HEIGHT)
#define SSD2677_STREAM_IN_BYTES 512U
#define SSD2677_STREAM_OUT_BYTES (SSD2677_STREAM_IN_BYTES * 2U)


static const char *TAG = "ssd2677";

static uint64_t driver_log_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}

static uint64_t driver_log_us(void)
{
    return (uint64_t)esp_timer_get_time();
}

static uint64_t driver_elapsed_ms(uint64_t start_us)
{
    return (driver_log_us() - start_us) / 1000ULL;
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
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, command), TAG, "write command failed");
    return seeed_epaper_panel_write_data(panel, &value, 1);
}

static void ssd2677_release_cs(seeed_epaper_panel_t *panel)
{
    if (panel->config.manual_cs && panel->config.pin_cs >= 0) {
        gpio_set_level(panel->config.pin_cs, 1);
    }
}

static void pack_interleave(uint8_t high_plane, uint8_t low_plane, uint8_t *out)
{
    out[0] = 0;
    out[1] = 0;

    for (uint8_t i = 0; i < 8U; i++) {
        const uint8_t pair = (uint8_t)((((high_plane >> (7U - i)) & 0x01U) << 1U) |
                                       ((low_plane >> (7U - i)) & 0x01U));
        if (i < 4U) {
            out[0] |= (uint8_t)(pair << (6U - i * 2U));
        } else {
            out[1] |= (uint8_t)(pair << (14U - i * 2U));
        }
    }
}

static void pack_mono_byte_to_2bpp(uint8_t mono, uint8_t *out)
{
    out[0] = 0;
    out[1] = 0;

    for (uint8_t i = 0; i < 8U; i++) {
        const uint8_t bit = (uint8_t)((mono >> (7U - i)) & 0x01U);
        const uint8_t pair = bit != 0U ? 0x03U : 0x00U;
        if (i < 4U) {
            out[0] |= (uint8_t)(pair << (6U - i * 2U));
        } else {
            out[1] |= (uint8_t)(pair << (14U - i * 2U));
        }
    }
}

static uint8_t ssd2677_mono_full_waveform_for_temperature(uint8_t temp)
{
    /* 3D97 V2: EPD_Slow_Display_1BIT(). */
    if (temp <= 10U) {
        return 0xE8U;
    }
    if (temp <= 20U) {
        return 0xEBU;
    }
    if (temp <= 30U) {
        return 0xEEU;
    }
    if (temp <= 127U) {
        return 0xF1U;
    }
    return 0xE8U;
}

static uint8_t ssd2677_gray4_waveform_for_temperature(uint8_t temp)
{
    /* 3D97 V2: EPD_Four_Gray_Display(). */
    if (temp <= 15U) {
        return 0xF1U;
    }
    if (temp <= 127U) {
        return 0xFAU;
    }
    return 0xF1U;
}

static uint8_t ssd2677_partial_waveform_for_temperature(uint8_t temp)
{
    /* 3D97 V2: dis_partial_img() and Fast_WHITE_TO_A(). */
    if (temp <= 10U) {
        return 0x08U;
    }
    if (temp <= 20U) {
        return 0x0FU;
    }
    if (temp <= 30U) {
        return 0x19U;
    }
    if (temp <= 127U) {
        return 0x23U;
    }
    return 0x08U;
}

static esp_err_t ssd2677_read_temperature_raw(seeed_epaper_panel_t *panel, uint8_t *ret_temp)
{
    uint8_t temp = 0;

    ESP_RETURN_ON_FALSE(ret_temp != NULL, ESP_ERR_INVALID_ARG, TAG, "ret_temp is null");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x40), TAG, "write temperature command failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_wait_ready(panel), TAG, "wait temperature failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_read_data(panel, &temp), TAG, "read temperature failed");

    *ret_temp = temp;
    return ESP_OK;
}

static uint8_t clamp_external_temperature(float temperature_c)
{
    if (!(temperature_c > 0.0f)) {
        return 0U;
    }
    if (temperature_c >= 127.0f) {
        return 127U;
    }
    return (uint8_t)(temperature_c + 0.5f);
}

static esp_err_t ssd2677_get_waveform_temperature(seeed_epaper_panel_t *panel,
                                                  uint8_t *ret_temp,
                                                  const char **ret_source)
{
    ESP_RETURN_ON_FALSE(ret_temp != NULL, ESP_ERR_INVALID_ARG, TAG, "ret_temp is null");
    ESP_RETURN_ON_FALSE(ret_source != NULL, ESP_ERR_INVALID_ARG, TAG, "ret_source is null");

    if (panel->external_temperature_valid) {
        *ret_temp = clamp_external_temperature(panel->external_temperature_c);
        *ret_source = "external";
        return ESP_OK;
    }

    *ret_source = "internal";
    return ssd2677_read_temperature_raw(panel, ret_temp);
}

static esp_err_t ssd2677_init_otp(seeed_epaper_panel_t *panel)
{
    static const uint8_t psr[] = {0x2F, 0x0E};
    static const uint8_t booster[] = {0x0F, 0x8B, 0x93, 0xC1};
    static const uint8_t timing[] = {0x76, 0x76, 0x76, 0x5A, 0x9D, 0x8A, 0x76, 0x62};
    static const uint8_t resolution[] = {0x03, 0x20, 0x02, 0xA8};
    static const uint8_t gsst[] = {0x00, 0x00, 0x00, 0x00};

    ESP_RETURN_ON_ERROR(seeed_epaper_panel_reset(panel), TAG, "reset failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_wait_ready(panel), TAG, "wait after reset failed");

    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x00), TAG, "write PSR failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_data(panel, psr, sizeof(psr)), TAG, "write PSR data failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_wait_ready(panel), TAG, "wait PSR failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x06), TAG, "write booster failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_data(panel, booster, sizeof(booster)), TAG, "write booster data failed");
    ESP_RETURN_ON_ERROR(write_reg8(panel, 0xE7, 0xC1), TAG, "write E7 failed");
    ESP_RETURN_ON_ERROR(write_reg8(panel, 0x30, 0x08), TAG, "write PLL failed");
    ESP_RETURN_ON_ERROR(write_reg8(panel, 0x50, 0x77), TAG, "write CDI failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x62), TAG, "write timing failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_data(panel, timing, sizeof(timing)), TAG, "write timing data failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x61), TAG, "write resolution failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_data(panel, resolution, sizeof(resolution)), TAG, "write resolution data failed");
    ESP_RETURN_ON_ERROR(write_reg8(panel, 0xE0, 0x10), TAG, "write E0 failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x65), TAG, "write GSST failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_data(panel, gsst, sizeof(gsst)), TAG, "write GSST data failed");
    ESP_RETURN_ON_ERROR(write_reg8(panel, 0xE9, 0x01), TAG, "write E9 failed");
    return seeed_epaper_panel_wait_ready(panel);
}

static esp_err_t ssd2677_init_mode(seeed_epaper_panel_t *panel, seeed_epaper_refresh_mode_t mode)
{
    const uint64_t start_ms = driver_log_ms();
    APP_EPAPER_DEBUG_LOGI(TAG, "init start: mode=%s", driver_mode_name(mode));
    esp_err_t err = ssd2677_init_otp(panel);
    APP_EPAPER_DEBUG_LOGI(TAG,
                          "init done: mode=%s elapsed=%llums err=%s",
                          driver_mode_name(mode),
                          (unsigned long long)(driver_log_ms() - start_ms),
                          esp_err_to_name(err));
    return err;
}

static esp_err_t ssd2677_set_window(seeed_epaper_panel_t *panel, const seeed_epaper_area_t *area)
{
    (void)panel;
    APP_EPAPER_DEBUG_LOGI(TAG,
                          "set_window: x=%u y=%u w=%u h=%u",
                          (unsigned)area->x,
                          (unsigned)area->y,
                          (unsigned)area->width,
                          (unsigned)area->height);
    return ESP_OK;
}

static esp_err_t ssd2677_write_image(seeed_epaper_panel_t *panel,
                                     const uint8_t *buffer,
                                     size_t len,
                                     bool previous_plane)
{
    (void)panel;
    (void)buffer;
    APP_EPAPER_DEBUG_LOGI(TAG,
                          "write_image staged: plane=%s len=%u",
                          previous_plane ? "previous" : "current",
                          (unsigned)len);
    return ESP_OK;
}

static esp_err_t ssd2677_power_refresh(seeed_epaper_panel_t *panel,
                                       seeed_epaper_refresh_mode_t mode)
{
    const uint8_t zero = 0x00U;
    const uint64_t start_us = driver_log_us();
    uint64_t t;
    uint64_t power_on_ms = 0;
    uint64_t power_on_settle_ms = 0;
    uint64_t refresh_ms;
    uint64_t power_off_ms = 0;
    uint64_t power_off_settle_ms = 0;

    if (!panel->refresh_powered) {
        t = driver_log_us();
        ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x04), TAG, "power on failed");
        ESP_RETURN_ON_ERROR(seeed_epaper_panel_wait_ready(panel), TAG, "wait power on failed");
        power_on_ms = driver_elapsed_ms(t);
        panel->last_refresh_timing.power_on_ms = (uint32_t)power_on_ms;
        panel->refresh_powered = true;
    }

    t = driver_log_us();
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x12), TAG, "display refresh failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_data(panel, &zero, 1), TAG, "display refresh data failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_wait_ready(panel), TAG, "wait display refresh failed");
    refresh_ms = driver_elapsed_ms(t);
    panel->last_refresh_timing.refresh_ms = (uint32_t)refresh_ms;

    if (mode != SEEED_EPAPER_REFRESH_PARTIAL) {
        t = driver_log_us();
        ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x02), TAG, "power off failed");
        ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_data(panel, &zero, 1), TAG, "power off data failed");
        ESP_RETURN_ON_ERROR(seeed_epaper_panel_wait_ready(panel), TAG, "wait power off failed");
        power_off_ms = driver_elapsed_ms(t);
        panel->last_refresh_timing.power_off_ms = (uint32_t)power_off_ms;
        panel->refresh_powered = false;
    }

    ssd2677_release_cs(panel);
    APP_EPAPER_DEBUG_LOGI(TAG,
             "[perf] power on=%llums settle=%llums refresh=%llums off=%llums settle=%llums total=%llums",
             (unsigned long long)power_on_ms,
             (unsigned long long)power_on_settle_ms,
             (unsigned long long)refresh_ms,
             (unsigned long long)power_off_ms,
             (unsigned long long)power_off_settle_ms,
             (unsigned long long)driver_elapsed_ms(start_us));
    return ESP_OK;
}

static esp_err_t ssd2677_latch_full_refresh_waveform(seeed_epaper_panel_t *panel)
{
    const uint64_t start_us = driver_log_us();
    uint8_t waveform = 0;
    uint8_t raw_temp = 0;
    const char *temp_source = "internal";
    uint64_t t = driver_log_us();

    ESP_RETURN_ON_ERROR(ssd2677_get_waveform_temperature(panel, &raw_temp, &temp_source), TAG, "temperature read failed");
    const uint64_t temperature_ms = driver_elapsed_ms(t);
    panel->last_refresh_timing.temperature_valid = true;
    panel->last_refresh_timing.temperature_external = panel->external_temperature_valid;
    panel->last_refresh_timing.temperature_c = raw_temp;
    panel->last_refresh_timing.temperature_ms = (uint32_t)temperature_ms;
    waveform = ssd2677_mono_full_waveform_for_temperature(raw_temp);
    /*
    ESP_LOGI(TAG,
             "latch full: temp=%u source=%s bucket=%u",
             (unsigned)raw_temp,
             temp_source,
             (unsigned)waveform);
    */
    t = driver_log_us();
    ESP_RETURN_ON_ERROR(write_reg8(panel, 0xE0, 0x12), TAG, "write full E0 failed");
    ESP_RETURN_ON_ERROR(write_reg8(panel, 0xE6, waveform), TAG, "write full E6 failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0xA5), TAG, "write full A5 failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_wait_ready(panel), TAG, "wait full A5 failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    panel->last_refresh_timing.latch_apply_ms = (uint32_t)driver_elapsed_ms(t);
    APP_EPAPER_DEBUG_LOGI(TAG,
             "[perf] latch full temp_read=%llums apply=%llums total=%llums",
             (unsigned long long)temperature_ms,
             (unsigned long long)driver_elapsed_ms(t),
             (unsigned long long)driver_elapsed_ms(start_us));
    return ESP_OK;
}

static esp_err_t ssd2677_latch_gray4_waveform(seeed_epaper_panel_t *panel)
{
    const uint64_t start_us = driver_log_us();
    uint8_t waveform = 0;
    uint8_t raw_temp = 0;
    const char *temp_source = "internal";
    uint64_t t = driver_log_us();

    ESP_RETURN_ON_ERROR(ssd2677_get_waveform_temperature(panel, &raw_temp, &temp_source), TAG, "temperature read failed");
    const uint64_t temperature_ms = driver_elapsed_ms(t);
    panel->last_refresh_timing.temperature_valid = true;
    panel->last_refresh_timing.temperature_external = panel->external_temperature_valid;
    panel->last_refresh_timing.temperature_c = raw_temp;
    panel->last_refresh_timing.temperature_ms = (uint32_t)temperature_ms;
    waveform = ssd2677_gray4_waveform_for_temperature(raw_temp);
    /*
    ESP_LOGI(TAG,
             "latch gray4: temp=%u source=%s bucket=%u",
             (unsigned)raw_temp,
             temp_source,
             (unsigned)waveform);
    */
    t = driver_log_us();
    ESP_RETURN_ON_ERROR(write_reg8(panel, 0xE0, 0x12), TAG, "write gray4 E0 failed");
    ESP_RETURN_ON_ERROR(write_reg8(panel, 0xE6, waveform), TAG, "write gray4 E6 failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0xA5), TAG, "write gray4 A5 failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_wait_ready(panel), TAG, "wait gray4 A5 failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    panel->last_refresh_timing.latch_apply_ms = (uint32_t)driver_elapsed_ms(t);
    APP_EPAPER_DEBUG_LOGI(TAG,
             "[perf] latch gray4 temp_read=%llums apply=%llums total=%llums",
             (unsigned long long)temperature_ms,
             (unsigned long long)driver_elapsed_ms(t),
             (unsigned long long)driver_elapsed_ms(start_us));
    return ESP_OK;
}

static esp_err_t ssd2677_latch_partial_waveform(seeed_epaper_panel_t *panel)
{
    const uint64_t start_us = driver_log_us();
    uint8_t temp = 0;
    uint8_t waveform = 0;
    const char *temp_source = "internal";
    uint64_t t = driver_log_us();

    ESP_RETURN_ON_ERROR(ssd2677_get_waveform_temperature(panel, &temp, &temp_source), TAG, "read temperature failed");
    const uint64_t temperature_ms = driver_elapsed_ms(t);
    panel->last_refresh_timing.temperature_valid = true;
    panel->last_refresh_timing.temperature_external = panel->external_temperature_valid;
    panel->last_refresh_timing.temperature_c = temp;
    panel->last_refresh_timing.temperature_ms = (uint32_t)temperature_ms;
    waveform = ssd2677_partial_waveform_for_temperature(temp);
    /*
    ESP_LOGI(TAG,
             "latch partial: temp=%u source=%s bucket=%u",
             (unsigned)temp,
             temp_source,
             (unsigned)waveform);
    */
    t = driver_log_us();
    ESP_RETURN_ON_ERROR(write_reg8(panel, 0xE0, 0x12), TAG, "write partial E0 failed");
    ESP_RETURN_ON_ERROR(write_reg8(panel, 0xE6, waveform), TAG, "write partial E6 failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0xA5), TAG, "write partial A5 failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_wait_ready(panel), TAG, "wait partial A5 failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    panel->last_refresh_timing.latch_apply_ms = (uint32_t)driver_elapsed_ms(t);
    APP_EPAPER_DEBUG_LOGI(TAG,
             "[perf] latch partial temp_read=%llums apply=%llums total=%llums",
             (unsigned long long)temperature_ms,
             (unsigned long long)driver_elapsed_ms(t),
             (unsigned long long)driver_elapsed_ms(start_us));
    return ESP_OK;
}

static esp_err_t ssd2677_write_full_framebuffer_1bpp(seeed_epaper_panel_t *panel)
{
    ESP_RETURN_ON_FALSE(panel->framebuffer != NULL, ESP_ERR_INVALID_STATE, TAG, "framebuffer is null");
    ESP_RETURN_ON_FALSE(panel->framebuffer_len >= SSD2677_1BPP_LEN, ESP_ERR_INVALID_SIZE, TAG, "framebuffer too small");

    uint8_t out[SSD2677_STREAM_OUT_BYTES];
    size_t src_offset = 0;
    size_t chunk_count = 0;
    uint64_t encode_us = 0;
    uint64_t tx_us = 0;
    const uint64_t total_start_us = driver_log_us();
    uint64_t t = driver_log_us();

    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x10), TAG, "write full DTM1 failed");
    const uint64_t command_us = driver_log_us() - t;
    panel->last_refresh_timing.stream_command_ms = (uint32_t)(command_us / 1000ULL);
    while (src_offset < SSD2677_1BPP_LEN) {
        const size_t in_chunk = (SSD2677_1BPP_LEN - src_offset) > SSD2677_STREAM_IN_BYTES ?
                                SSD2677_STREAM_IN_BYTES :
                                (SSD2677_1BPP_LEN - src_offset);
        t = driver_log_us();
        for (size_t i = 0; i < in_chunk; i++) {
            pack_mono_byte_to_2bpp(panel->framebuffer[src_offset + i], &out[i * 2U]);
        }
        encode_us += driver_log_us() - t;
        t = driver_log_us();
        ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_data(panel, out, in_chunk * 2U), TAG, "write full framebuffer failed");
        tx_us += driver_log_us() - t;
        src_offset += in_chunk;
        ++chunk_count;
    }
    panel->last_refresh_timing.stream_encode_ms = (uint32_t)(encode_us / 1000ULL);
    panel->last_refresh_timing.stream_spi_ms = (uint32_t)(tx_us / 1000ULL);
    APP_EPAPER_DEBUG_LOGI(TAG,
             "[perf] stream full input=%uB output=%uB chunks=%u cmd=%llums encode=%llums spi=%llums total=%llums",
             (unsigned)SSD2677_1BPP_LEN,
             (unsigned)SSD2677_2BPP_LEN,
             (unsigned)chunk_count,
             (unsigned long long)(command_us / 1000ULL),
             (unsigned long long)(encode_us / 1000ULL),
             (unsigned long long)(tx_us / 1000ULL),
             (unsigned long long)driver_elapsed_ms(total_start_us));
    return ESP_OK;
}

static esp_err_t ssd2677_write_full_diff_framebuffer_1bpp(seeed_epaper_panel_t *panel)
{
    ESP_RETURN_ON_FALSE(panel->framebuffer != NULL, ESP_ERR_INVALID_STATE, TAG, "framebuffer is null");
    ESP_RETURN_ON_FALSE(panel->previous_framebuffer != NULL, ESP_ERR_INVALID_STATE, TAG, "previous_framebuffer is null");
    ESP_RETURN_ON_FALSE(panel->framebuffer_len >= SSD2677_1BPP_LEN, ESP_ERR_INVALID_SIZE, TAG, "framebuffer too small");
    ESP_RETURN_ON_FALSE(panel->previous_framebuffer_len >= SSD2677_1BPP_LEN, ESP_ERR_INVALID_SIZE, TAG, "previous framebuffer too small");

    uint8_t out[SSD2677_STREAM_OUT_BYTES];
    size_t src_offset = 0;
    size_t chunk_count = 0;
    uint64_t encode_us = 0;
    uint64_t tx_us = 0;
    const uint64_t total_start_us = driver_log_us();
    uint64_t t = driver_log_us();

    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x10), TAG, "write full diff DTM1 failed");
    const uint64_t command_us = driver_log_us() - t;
    panel->last_refresh_timing.stream_command_ms = (uint32_t)(command_us / 1000ULL);
    while (src_offset < SSD2677_1BPP_LEN) {
        const size_t in_chunk = (SSD2677_1BPP_LEN - src_offset) > SSD2677_STREAM_IN_BYTES ?
                                SSD2677_STREAM_IN_BYTES :
                                (SSD2677_1BPP_LEN - src_offset);
        t = driver_log_us();
        for (size_t i = 0; i < in_chunk; i++) {
            pack_interleave(panel->previous_framebuffer[src_offset + i],
                            panel->framebuffer[src_offset + i],
                            &out[i * 2U]);
        }
        encode_us += driver_log_us() - t;
        t = driver_log_us();
        ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_data(panel, out, in_chunk * 2U), TAG, "write full diff data failed");
        tx_us += driver_log_us() - t;
        src_offset += in_chunk;
        ++chunk_count;
    }
    panel->last_refresh_timing.stream_encode_ms = (uint32_t)(encode_us / 1000ULL);
    panel->last_refresh_timing.stream_spi_ms = (uint32_t)(tx_us / 1000ULL);
    APP_EPAPER_DEBUG_LOGI(TAG,
             "[perf] stream partial input=%uB output=%uB chunks=%u cmd=%llums encode=%llums spi=%llums total=%llums",
             (unsigned)SSD2677_1BPP_LEN,
             (unsigned)SSD2677_2BPP_LEN,
             (unsigned)chunk_count,
             (unsigned long long)(command_us / 1000ULL),
             (unsigned long long)(encode_us / 1000ULL),
             (unsigned long long)(tx_us / 1000ULL),
             (unsigned long long)driver_elapsed_ms(total_start_us));
    return ESP_OK;
}

static esp_err_t ssd2677_write_full_framebuffer_gray4(seeed_epaper_panel_t *panel)
{
    ESP_RETURN_ON_FALSE(panel->gray4_framebuffer != NULL, ESP_ERR_INVALID_STATE, TAG, "gray4 framebuffer is null");
    ESP_RETURN_ON_FALSE(panel->gray4_valid, ESP_ERR_INVALID_STATE, TAG, "gray4 framebuffer is invalid");
    ESP_RETURN_ON_FALSE(panel->gray4_framebuffer_len >= SSD2677_2BPP_LEN, ESP_ERR_INVALID_SIZE, TAG, "gray4 framebuffer too small");

    const uint64_t total_start_us = driver_log_us();
    uint64_t t = driver_log_us();
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x10), TAG, "write gray4 DTM1 failed");
    const uint64_t command_us = driver_log_us() - t;
    panel->last_refresh_timing.stream_command_ms = (uint32_t)(command_us / 1000ULL);
    t = driver_log_us();
    const esp_err_t err = seeed_epaper_panel_write_data(panel, panel->gray4_framebuffer, SSD2677_2BPP_LEN);
    const uint64_t tx_us = driver_log_us() - t;
    panel->last_refresh_timing.stream_spi_ms = (uint32_t)(tx_us / 1000ULL);
    ESP_RETURN_ON_ERROR(err, TAG, "write gray4 framebuffer failed");
    APP_EPAPER_DEBUG_LOGI(TAG,
             "[perf] stream gray4 input=%uB output=%uB chunks=1 cmd=%llums spi=%llums total=%llums",
             (unsigned)SSD2677_2BPP_LEN,
             (unsigned)SSD2677_2BPP_LEN,
             (unsigned long long)(command_us / 1000ULL),
             (unsigned long long)(tx_us / 1000ULL),
             (unsigned long long)driver_elapsed_ms(total_start_us));
    return ESP_OK;
}

static esp_err_t ssd2677_refresh(seeed_epaper_panel_t *panel, seeed_epaper_refresh_mode_t mode)
{
    const uint64_t start_ms = driver_log_ms();
    uint64_t phase_ms;
    uint64_t latch_ms;
    uint64_t stream_ms;
    uint64_t drive_ms;

    panel->last_refresh_timing = (seeed_epaper_refresh_timing_t){0};

    APP_EPAPER_DEBUG_LOGI(TAG, "[perf] refresh start: mode=%s", driver_mode_name(mode));

    /* ── Phase 1: waveform latch ── */
    phase_ms = driver_log_ms();
    switch (mode) {
    case SEEED_EPAPER_REFRESH_PARTIAL:
        ESP_RETURN_ON_ERROR(ssd2677_latch_partial_waveform(panel), TAG, "partial latch failed");
        break;
    case SEEED_EPAPER_REFRESH_GRAY4:
        ESP_RETURN_ON_ERROR(ssd2677_latch_gray4_waveform(panel), TAG, "gray4 latch failed");
        break;
    case SEEED_EPAPER_REFRESH_FULL:
    default:
        ESP_RETURN_ON_ERROR(ssd2677_latch_full_refresh_waveform(panel), TAG, "full latch failed");
        break;
    }
    latch_ms = driver_log_ms() - phase_ms;
    panel->last_refresh_timing.latch_ms = (uint32_t)latch_ms;
    APP_EPAPER_DEBUG_LOGI(TAG, "[perf] refresh phase1-latch=%llums",
                          (unsigned long long)latch_ms);

    /* ── Phase 2: framebuffer write ── */
    phase_ms = driver_log_ms();
    switch (mode) {
    case SEEED_EPAPER_REFRESH_PARTIAL:
        ESP_RETURN_ON_ERROR(ssd2677_write_full_diff_framebuffer_1bpp(panel), TAG, "write full diff framebuffer failed");
        break;
    case SEEED_EPAPER_REFRESH_GRAY4:
        ESP_RETURN_ON_ERROR(ssd2677_write_full_framebuffer_gray4(panel), TAG, "write gray4 framebuffer failed");
        break;
    case SEEED_EPAPER_REFRESH_FULL:
    default:
        ESP_RETURN_ON_ERROR(ssd2677_write_full_framebuffer_1bpp(panel), TAG, "write full framebuffer failed");
        break;
    }
    stream_ms = driver_log_ms() - phase_ms;
    panel->last_refresh_timing.stream_ms = (uint32_t)stream_ms;
    APP_EPAPER_DEBUG_LOGI(TAG, "[perf] refresh phase2-stream=%llums",
                          (unsigned long long)stream_ms);

    /* Keep high voltage enabled across consecutive partial refreshes. */
    phase_ms = driver_log_ms();
    ESP_RETURN_ON_ERROR(ssd2677_power_refresh(panel, mode), TAG, "refresh sequence failed");
    drive_ms = driver_log_ms() - phase_ms;
    panel->last_refresh_timing.drive_ms = (uint32_t)drive_ms;
    APP_EPAPER_DEBUG_LOGI(TAG, "[perf] refresh phase3-drive=%llums",
                          (unsigned long long)drive_ms);

    APP_EPAPER_DEBUG_LOGI(TAG,
                          "[perf] refresh mode=%s total=%llums latch=%llums stream=%llums drive=%llums",
                          driver_mode_name(mode),
                          (unsigned long long)(driver_log_ms() - start_ms),
                          (unsigned long long)latch_ms,
                          (unsigned long long)stream_ms,
                          (unsigned long long)drive_ms);
    return ESP_OK;
}

static esp_err_t ssd2677_sleep(seeed_epaper_panel_t *panel)
{
    const uint8_t data = 0xA5U;
    const uint8_t zero = 0x00U;

    if (panel->refresh_powered) {
        ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x02), TAG, "power off before sleep failed");
        ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_data(panel, &zero, 1), TAG, "power off data before sleep failed");
        ESP_RETURN_ON_ERROR(seeed_epaper_panel_wait_ready(panel), TAG, "wait power off before sleep failed");
        panel->refresh_powered = false;
    }

    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x07), TAG, "deep sleep command failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_data(panel, &data, 1), TAG, "deep sleep data failed");
    ssd2677_release_cs(panel);
    return ESP_OK;
}

static esp_err_t ssd2677_standby(seeed_epaper_panel_t *panel)
{
    const uint64_t start_ms = driver_log_ms();
    const uint8_t zero = 0x00U;

    APP_EPAPER_DEBUG_LOGI(TAG, "standby start");
    esp_err_t err = ESP_OK;
    if (panel->refresh_powered) {
        ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x02), TAG, "power off failed");
        ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_data(panel, &zero, 1), TAG, "power off data failed");
        err = seeed_epaper_panel_wait_ready(panel);
        if (err == ESP_OK) {
            panel->refresh_powered = false;
        }
    }
    ssd2677_release_cs(panel);
    APP_EPAPER_DEBUG_LOGI(TAG,
                          "standby done: elapsed=%llums err=%s",
                          (unsigned long long)(driver_log_ms() - start_ms),
                          esp_err_to_name(err));
    return err;
}

static esp_err_t ssd2677_resume(seeed_epaper_panel_t *panel)
{
    (void)panel;
    APP_EPAPER_DEBUG_LOGI(TAG, "resume done: refresh path powers on panel when needed");
    return ESP_OK;
}

const seeed_epaper_driver_t *seeed_epaper_ssd2677_driver(void)
{
    static const seeed_epaper_driver_t driver = {
        .name = "SSD2677",
        .info = {
            .width = SSD2677_WIDTH,
            .height = SSD2677_HEIGHT,
            .native_bpp = 1,
            .supports_partial = true,
            .supports_gray4 = true,
            .supports_color = false,
        },
        .init_mode = ssd2677_init_mode,
        .set_window = ssd2677_set_window,
        .write_image = ssd2677_write_image,
        .refresh = ssd2677_refresh,
        .sleep = ssd2677_sleep,
        .standby = ssd2677_standby,
        .resume = ssd2677_resume,
    };
    return &driver;
}
