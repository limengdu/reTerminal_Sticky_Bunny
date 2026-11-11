#include "epaper_panel_priv.h"

#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_debug_log.h"

#define SEEED_EPAPER_SPI_CHUNK_BYTES 1024U

static const char *TAG = "seeed_epaper";
static const uint32_t kPreferredBufferCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
static const uint32_t kFallbackBufferCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
static const uint32_t kSpiTxBufferCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT;
static const uint32_t kSsd1677BusyTimeoutMs = 8000U;
static const uint16_t kSsd1677ResetLowMs = 10U;
static const uint16_t kSsd1677ResetHighMs = 10U;
static const uint32_t kSsd2677BusyTimeoutMs = 30000U;
static const uint16_t kSsd2677ResetLowMs = 20U;
static const uint16_t kSsd2677ResetHighMs = 10U;
static const uint8_t kSsd2677ChipId = 0x07U;
static atomic_bool s_busy_wait_active = ATOMIC_VAR_INIT(false);

static size_t packed_mono_stride(uint16_t width);
static size_t packed_2bpp_stride(uint16_t width);

static uint64_t epaper_log_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}

bool seeed_epaper_panel_busy_wait_active(void)
{
    return atomic_load_explicit(&s_busy_wait_active, memory_order_acquire);
}

static const char *refresh_mode_name(seeed_epaper_refresh_mode_t mode)
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

static const char *pixel_format_name(seeed_epaper_pixel_format_t format)
{
    switch (format) {
    case SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB:
        return "mono1_msb";
    case SEEED_EPAPER_PIXEL_FORMAT_GRAY4:
        return "gray4";
    case SEEED_EPAPER_PIXEL_FORMAT_GRAY8:
        return "gray8";
    case SEEED_EPAPER_PIXEL_FORMAT_RGB565:
        return "rgb565";
    default:
        return "unknown";
    }
}

static void log_area(const char *prefix, const seeed_epaper_area_t *area)
{
    if (area == NULL) {
        APP_EPAPER_DEBUG_LOGI(TAG, "%s area=(null)", prefix);
        return;
    }

    APP_EPAPER_DEBUG_LOGI(TAG,
             "%s area={x=%u y=%u w=%u h=%u}",
             prefix,
             (unsigned)area->x,
             (unsigned)area->y,
             (unsigned)area->width,
             (unsigned)area->height);
}

static void mark_panel_needs_reinit(seeed_epaper_panel_t *panel)
{
    if (panel == NULL) {
        return;
    }

    panel->prepared = false;
    panel->sleeping = false;
    panel->standby = false;
    panel->partial_after_standby = false;
    panel->partial_ready = false;
    panel->refresh_powered = false;
    panel->dirty_valid = false;
    panel->gray4_valid = false;
    APP_EPAPER_DEBUG_LOGW(TAG, "panel marked for reinit");
}

static void *alloc_temp_buffer(size_t size)
{
    return heap_caps_malloc_prefer(size, 2, kPreferredBufferCaps, kFallbackBufferCaps);
}

static esp_err_t ensure_spi_tx_buffer(seeed_epaper_panel_t *panel)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");

    if (panel->spi_tx_buffer != NULL && panel->spi_tx_buffer_len >= SEEED_EPAPER_SPI_CHUNK_BYTES) {
        return ESP_OK;
    }

    if (panel->spi_tx_buffer != NULL) {
        heap_caps_free(panel->spi_tx_buffer);
        panel->spi_tx_buffer = NULL;
        panel->spi_tx_buffer_len = 0;
    }

    panel->spi_tx_buffer = (uint8_t *)heap_caps_malloc(SEEED_EPAPER_SPI_CHUNK_BYTES, kSpiTxBufferCaps);
    ESP_RETURN_ON_FALSE(panel->spi_tx_buffer != NULL, ESP_ERR_NO_MEM, TAG, "alloc spi tx buffer failed");
    panel->spi_tx_buffer_len = SEEED_EPAPER_SPI_CHUNK_BYTES;
    APP_EPAPER_DEBUG_LOGI(TAG, "spi tx buffer allocated: %u bytes", (unsigned)panel->spi_tx_buffer_len);
    return ESP_OK;
}

static esp_err_t ensure_framebuffer(seeed_epaper_panel_t *panel)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");

    const size_t stride = packed_mono_stride(panel->info.width);
    const size_t len = stride * panel->info.height;
    if (panel->framebuffer != NULL &&
        panel->framebuffer_len == len &&
        panel->previous_framebuffer != NULL &&
        panel->previous_framebuffer_len == len) {
        return ESP_OK;
    }

    if (panel->framebuffer != NULL) {
        heap_caps_free(panel->framebuffer);
        panel->framebuffer = NULL;
        panel->framebuffer_stride = 0;
        panel->framebuffer_len = 0;
    }
    if (panel->previous_framebuffer != NULL) {
        heap_caps_free(panel->previous_framebuffer);
        panel->previous_framebuffer = NULL;
        panel->previous_framebuffer_len = 0;
    }

    panel->framebuffer = (uint8_t *)alloc_temp_buffer(len);
    ESP_RETURN_ON_FALSE(panel->framebuffer != NULL, ESP_ERR_NO_MEM, TAG, "alloc framebuffer failed");
    memset(panel->framebuffer, 0xFF, len);
    panel->previous_framebuffer = (uint8_t *)alloc_temp_buffer(len);
    ESP_RETURN_ON_FALSE(panel->previous_framebuffer != NULL, ESP_ERR_NO_MEM, TAG, "alloc previous framebuffer failed");
    memset(panel->previous_framebuffer, 0xFF, len);
    panel->framebuffer_stride = stride;
    panel->framebuffer_len = len;
    panel->previous_framebuffer_len = len;
    APP_EPAPER_DEBUG_LOGI(TAG,
             "framebuffer allocated: %ux%u stride=%u len=%u",
             (unsigned)panel->info.width,
             (unsigned)panel->info.height,
             (unsigned)panel->framebuffer_stride,
             (unsigned)panel->framebuffer_len);
    return ESP_OK;
}

static esp_err_t ensure_gray4_framebuffer(seeed_epaper_panel_t *panel)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");

    const size_t stride = packed_2bpp_stride(panel->info.width);
    const size_t len = stride * panel->info.height;
    if (panel->gray4_framebuffer != NULL && panel->gray4_framebuffer_len == len) {
        return ESP_OK;
    }

    if (panel->gray4_framebuffer != NULL) {
        heap_caps_free(panel->gray4_framebuffer);
        panel->gray4_framebuffer = NULL;
        panel->gray4_framebuffer_len = 0;
    }

    panel->gray4_framebuffer = (uint8_t *)alloc_temp_buffer(len);
    ESP_RETURN_ON_FALSE(panel->gray4_framebuffer != NULL, ESP_ERR_NO_MEM, TAG, "alloc gray4 framebuffer failed");
    memset(panel->gray4_framebuffer, 0xFF, len);
    panel->gray4_framebuffer_len = len;
    APP_EPAPER_DEBUG_LOGI(TAG,
             "gray4 framebuffer allocated: %ux%u stride=%u len=%u",
             (unsigned)panel->info.width,
             (unsigned)panel->info.height,
             (unsigned)stride,
             (unsigned)len);
    return ESP_OK;
}

static esp_err_t copy_framebuffer_region(const seeed_epaper_panel_t *panel,
                                         const seeed_epaper_area_t *area,
                                         uint8_t *dst,
                                         size_t dst_len)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");
    ESP_RETURN_ON_FALSE(area != NULL, ESP_ERR_INVALID_ARG, TAG, "area is null");
    ESP_RETURN_ON_FALSE(dst != NULL, ESP_ERR_INVALID_ARG, TAG, "dst is null");
    ESP_RETURN_ON_FALSE(panel->framebuffer != NULL, ESP_ERR_INVALID_STATE, TAG, "framebuffer is null");
    ESP_RETURN_ON_FALSE((area->x & 0x7U) == 0U, ESP_ERR_INVALID_ARG, TAG, "area x must be byte aligned");
    ESP_RETURN_ON_FALSE((area->width & 0x7U) == 0U, ESP_ERR_INVALID_ARG, TAG, "area width must be byte aligned");

    const size_t row_bytes = packed_mono_stride(area->width);
    ESP_RETURN_ON_FALSE(dst_len >= row_bytes * area->height, ESP_ERR_INVALID_ARG, TAG, "dst buffer too small");

    for (uint16_t y = 0; y < area->height; ++y) {
        const size_t src_offset =
            ((size_t)(area->y + y) * panel->framebuffer_stride) + ((size_t)area->x >> 3);
        memcpy(dst + ((size_t)y * row_bytes), panel->framebuffer + src_offset, row_bytes);
    }
    return ESP_OK;
}

static esp_err_t update_framebuffer_region(seeed_epaper_panel_t *panel,
                                           const seeed_epaper_area_t *area,
                                           const uint8_t *src,
                                           size_t src_len)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");
    ESP_RETURN_ON_FALSE(area != NULL, ESP_ERR_INVALID_ARG, TAG, "area is null");
    ESP_RETURN_ON_FALSE(src != NULL, ESP_ERR_INVALID_ARG, TAG, "src is null");
    ESP_RETURN_ON_FALSE(panel->framebuffer != NULL, ESP_ERR_INVALID_STATE, TAG, "framebuffer is null");
    ESP_RETURN_ON_FALSE((area->x & 0x7U) == 0U, ESP_ERR_INVALID_ARG, TAG, "area x must be byte aligned");
    ESP_RETURN_ON_FALSE((area->width & 0x7U) == 0U, ESP_ERR_INVALID_ARG, TAG, "area width must be byte aligned");

    const size_t row_bytes = packed_mono_stride(area->width);
    ESP_RETURN_ON_FALSE(src_len >= row_bytes * area->height, ESP_ERR_INVALID_ARG, TAG, "src buffer too small");

    for (uint16_t y = 0; y < area->height; ++y) {
        const size_t dst_offset =
            ((size_t)(area->y + y) * panel->framebuffer_stride) + ((size_t)area->x >> 3);
        memcpy(panel->framebuffer + dst_offset, src + ((size_t)y * row_bytes), row_bytes);
    }
    return ESP_OK;
}

static esp_err_t update_framebuffer_region_from_gray4(seeed_epaper_panel_t *panel,
                                                      const seeed_epaper_area_t *area,
                                                      const uint8_t *src,
                                                      size_t src_stride_bytes)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");
    ESP_RETURN_ON_FALSE(area != NULL, ESP_ERR_INVALID_ARG, TAG, "area is null");
    ESP_RETURN_ON_FALSE(src != NULL, ESP_ERR_INVALID_ARG, TAG, "src is null");
    ESP_RETURN_ON_FALSE(panel->framebuffer != NULL, ESP_ERR_INVALID_STATE, TAG, "framebuffer is null");

    const size_t src_stride_2bpp = packed_2bpp_stride(area->width);
    const size_t src_stride_4bpp = (area->width + 1U) / 2U;
    const bool src_is_2bpp = (src_stride_bytes == src_stride_2bpp);
    const bool src_is_4bpp = (src_stride_bytes == src_stride_4bpp);
    ESP_RETURN_ON_FALSE(src_is_2bpp || src_is_4bpp, ESP_ERR_INVALID_ARG, TAG, "invalid gray4 stride");

    for (uint16_t y = 0; y < area->height; ++y) {
        for (uint16_t x = 0; x < area->width; ++x) {
            uint8_t gray4 = 0U;
            if (src_is_2bpp) {
                const uint8_t packed = src[(size_t)y * src_stride_bytes + ((size_t)x >> 2)];
                const uint8_t shift = (uint8_t)(6U - ((x & 0x03U) * 2U));
                gray4 = (uint8_t)((packed >> shift) & 0x03U);
            } else {
                const uint8_t packed = src[(size_t)y * src_stride_bytes + ((size_t)x >> 1)];
                gray4 = (x & 0x01U) == 0U ? (uint8_t)(packed >> 4) : (packed & 0x0FU);
            }

            const bool black = src_is_2bpp ? (gray4 < 2U) : (gray4 < 8U);
            const uint16_t logical_x = (uint16_t)(area->x + x);
            // The mono comparison shadow must use the same physical coordinates
            // as the SSD2677 gray4 stream, especially on the first partial update.
            // 黑白对比缓存与 SSD2677 灰度流必须使用相同的物理坐标。
            const uint16_t dst_x = panel->config.mirror_x
                                       ? (uint16_t)(panel->info.width - 1U - logical_x)
                                       : logical_x;
            const uint16_t dst_y = (uint16_t)(area->y + y);
            uint8_t *byte = &panel->framebuffer[(size_t)dst_y * panel->framebuffer_stride + ((size_t)dst_x >> 3)];
            const uint8_t mask = (uint8_t)(0x80U >> (dst_x & 0x07U));
            if (black) {
                *byte &= (uint8_t)~mask;
            } else {
                *byte |= mask;
            }
        }
    }

    return ESP_OK;
}

static seeed_epaper_refresh_mode_t resolve_refresh_mode(const seeed_epaper_panel_t *panel,
                                                        seeed_epaper_refresh_mode_t requested_mode)
{
    if ((panel->model == SEEED_EPAPER_PANEL_SSD1677 ||
         panel->model == SEEED_EPAPER_PANEL_SSD2677) &&
        (requested_mode == SEEED_EPAPER_REFRESH_PARTIAL) &&
        !panel->partial_ready) {
        APP_EPAPER_DEBUG_LOGW(TAG, "SSD partial requested before baseline full refresh, promoting to full");
        return SEEED_EPAPER_REFRESH_FULL;
    }
    return requested_mode;
}

static uint8_t reverse_bits_u8(uint8_t value)
{
    value = (uint8_t)(((value & 0xF0U) >> 4) | ((value & 0x0FU) << 4));
    value = (uint8_t)(((value & 0xCCU) >> 2) | ((value & 0x33U) << 2));
    value = (uint8_t)(((value & 0xAAU) >> 1) | ((value & 0x55U) << 1));
    return value;
}

static inline bool gpio_is_valid(gpio_num_t pin)
{
    return pin >= 0;
}

static bool ensure_gpio_isr_service(void)
{
    esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        return true;
    }

    APP_EPAPER_DEBUG_LOGW(TAG, "install GPIO ISR service failed: %s", esp_err_to_name(err));
    return false;
}

static void IRAM_ATTR busy_gpio_isr_handler(void *arg)
{
    seeed_epaper_panel_t *panel = (seeed_epaper_panel_t *)arg;
    if (panel == NULL || panel->busy_sem == NULL) {
        return;
    }

    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(panel->busy_sem, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static size_t packed_mono_stride(uint16_t width)
{
    return (size_t)((width + 7U) >> 3);
}

static size_t packed_2bpp_stride(uint16_t width)
{
    return (size_t)((width + 3U) >> 2);
}

static seeed_epaper_area_t transform_area_for_io(const seeed_epaper_panel_t *panel,
                                                 const seeed_epaper_area_t *area)
{
    seeed_epaper_area_t io_area = *area;
    if (panel->config.mirror_x) {
        io_area.x = (uint16_t)(panel->info.width - (area->x + area->width));
    }
    return io_area;
}

static size_t pixel_format_stride(uint16_t width, seeed_epaper_pixel_format_t format)
{
    switch (format) {
    case SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB:
        return packed_mono_stride(width);
    case SEEED_EPAPER_PIXEL_FORMAT_GRAY4:
        return (size_t)((width + 1U) >> 1);
    case SEEED_EPAPER_PIXEL_FORMAT_GRAY8:
        return (size_t)width;
    case SEEED_EPAPER_PIXEL_FORMAT_RGB565:
        return (size_t)width * sizeof(uint16_t);
    default:
        return 0;
    }
}

static bool source_pixel_is_white(const uint8_t *src,
                                  size_t stride,
                                  uint16_t x,
                                  uint16_t y,
                                  seeed_epaper_pixel_format_t format)
{
    const uint8_t *row = src + ((size_t)y * stride);

    switch (format) {
    case SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB: {
        const uint8_t byte = row[x >> 3];
        const uint8_t bit = (uint8_t)(7U - (x & 0x7U));
        return ((byte >> bit) & 0x1U) != 0;
    }
    case SEEED_EPAPER_PIXEL_FORMAT_GRAY4: {
        const uint8_t packed = row[x >> 1];
        const uint8_t gray4 = (x & 0x1U) == 0 ? (packed >> 4) : (packed & 0x0FU);
        return gray4 >= 8U;
    }
    case SEEED_EPAPER_PIXEL_FORMAT_GRAY8:
        return row[x] >= 128U;
    case SEEED_EPAPER_PIXEL_FORMAT_RGB565: {
        const uint16_t *row16 = (const uint16_t *)row;
        const uint16_t pixel = row16[x];
        const uint8_t r = (uint8_t)((((pixel >> 11) & 0x1FU) * 255U) / 31U);
        const uint8_t g = (uint8_t)((((pixel >> 5) & 0x3FU) * 255U) / 63U);
        const uint8_t b = (uint8_t)(((pixel & 0x1FU) * 255U) / 31U);
        const uint16_t luma = (uint16_t)((299U * r + 587U * g + 114U * b) / 1000U);
        return luma >= 128U;
    }
    default:
        return true;
    }
}

static esp_err_t build_native_1bpp(const seeed_epaper_area_t *src_area,
                                   const seeed_epaper_area_t *aligned_area,
                                   const void *pixels,
                                   size_t src_stride_bytes,
                                   seeed_epaper_pixel_format_t input_format,
                                   bool mirror_x,
                                   uint8_t **out_buffer,
                                   size_t *out_len)
{
    ESP_RETURN_ON_FALSE(src_area != NULL, ESP_ERR_INVALID_ARG, TAG, "src_area is null");
    ESP_RETURN_ON_FALSE(aligned_area != NULL, ESP_ERR_INVALID_ARG, TAG, "aligned_area is null");
    ESP_RETURN_ON_FALSE(pixels != NULL, ESP_ERR_INVALID_ARG, TAG, "pixels is null");
    ESP_RETURN_ON_FALSE(out_buffer != NULL && out_len != NULL, ESP_ERR_INVALID_ARG, TAG, "output is null");

    if (src_stride_bytes == 0U) {
        src_stride_bytes = pixel_format_stride(src_area->width, input_format);
    }
    ESP_RETURN_ON_FALSE(src_stride_bytes > 0U, ESP_ERR_INVALID_ARG, TAG, "invalid stride");

    const size_t dst_stride = packed_mono_stride(aligned_area->width);
    const size_t dst_len = dst_stride * aligned_area->height;
    uint8_t *dst = (uint8_t *)alloc_temp_buffer(dst_len);
    ESP_RETURN_ON_FALSE(dst != NULL, ESP_ERR_NO_MEM, TAG, "alloc native buffer failed");

    const uint16_t x_offset = (uint16_t)(src_area->x - aligned_area->x);
    const uint8_t *src = (const uint8_t *)pixels;

    const bool direct_mono_copy =
        input_format == SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB &&
        x_offset == 0U &&
        src_area->width == aligned_area->width &&
        src_area->height == aligned_area->height;
    if (direct_mono_copy) {
        // The UI shadow buffers are already byte-aligned Mono1 data.
        for (uint16_t y = 0; y < aligned_area->height; y++) {
            const uint8_t *src_row = src + ((size_t)y * src_stride_bytes);
            uint8_t *dst_row = dst + ((size_t)y * dst_stride);
            if (mirror_x) {
                for (size_t x = 0; x < dst_stride; x++) {
                    dst_row[dst_stride - x - 1U] = reverse_bits_u8(src_row[x]);
                }
            } else {
                memcpy(dst_row, src_row, dst_stride);
            }
        }
    } else {
        memset(dst, 0xFF, dst_len);
        for (uint16_t y = 0; y < src_area->height; y++) {
            for (uint16_t x = 0; x < src_area->width; x++) {
                const bool white = source_pixel_is_white(src, src_stride_bytes, x, y, input_format);
                const uint16_t dst_x = (uint16_t)(x + x_offset);
                const size_t index = ((size_t)y * dst_stride) + (dst_x >> 3);
                const uint8_t bit = (uint8_t)(7U - (dst_x & 0x7U));
                if (white) {
                    dst[index] |= (uint8_t)(1U << bit);
                } else {
                    dst[index] &= (uint8_t)~(1U << bit);
                }
            }
        }

        if (mirror_x) {
            seeed_epaper_panel_mirror_x(dst, aligned_area->width, aligned_area->height);
        }
    }

    *out_buffer = dst;
    *out_len = dst_len;
    return ESP_OK;
}

//4_gray_surpport: build DTM1/DTM2 bit planes from 2bpp gray4 data
static esp_err_t build_native_gray4_planes(
    const seeed_epaper_area_t *src_area,
    const seeed_epaper_area_t *aligned_area,
    const uint8_t *pixels,
    size_t src_stride_bytes,
    bool mirror_x,
    uint8_t **dtm1,
    size_t *dtm1_len,
    uint8_t **dtm2,
    size_t *dtm2_len)
{
    ESP_RETURN_ON_FALSE(src_area != NULL, ESP_ERR_INVALID_ARG, TAG, "src_area is null");
    ESP_RETURN_ON_FALSE(aligned_area != NULL, ESP_ERR_INVALID_ARG, TAG, "aligned_area is null");
    ESP_RETURN_ON_FALSE(pixels != NULL, ESP_ERR_INVALID_ARG, TAG, "pixels is null");
    ESP_RETURN_ON_FALSE(dtm1 != NULL && dtm1_len != NULL, ESP_ERR_INVALID_ARG, TAG, "dtm1 output is null");
    ESP_RETURN_ON_FALSE(dtm2 != NULL && dtm2_len != NULL, ESP_ERR_INVALID_ARG, TAG, "dtm2 output is null");

    // DTM1/DTM2 是 1bpp 格式，每行字节数 = (width + 7) / 8
    const size_t dtm_stride = packed_mono_stride(aligned_area->width);
    const size_t dtm_size = dtm_stride * aligned_area->height;

    // 分配 DTM1 缓冲区（使用 DMA 内存，因为要通过 SPI 发送）
    *dtm1 = (uint8_t *)alloc_temp_buffer(dtm_size);
    ESP_RETURN_ON_FALSE(*dtm1 != NULL, ESP_ERR_NO_MEM, TAG, "alloc dtm1 buffer failed");
    memset(*dtm1, 0xFF, dtm_size);  // 初始化为全白 (bit=1 是白)

    // 分配 DTM2 缓冲区
    *dtm2 = (uint8_t *)alloc_temp_buffer(dtm_size);
    if (*dtm2 == NULL) {
        heap_caps_free(*dtm1);
        *dtm1 = NULL;
        ESP_LOGE(TAG, "alloc dtm2 buffer failed");
        return ESP_ERR_NO_MEM;
    }
    memset(*dtm2, 0xFF, dtm_size);  // 初始化为全白

    *dtm1_len = dtm_size;
    *dtm2_len = dtm_size;

    // x_offset: 源区域起点相对于对齐区域起点的偏移
    const uint16_t x_offset = (uint16_t)(src_area->x - aligned_area->x);

    // 遍历每个像素，提取 bit1→DTM1, bit0→DTM2
    for (uint16_t y = 0; y < src_area->height; y++) {
        for (uint16_t x = 0; x < src_area->width; x++) {
            // 从 2bpp packed 数据中读取一个像素
            // 每字节 4 个像素，MSB 在前
            // byte = [px0_bit1|px0_bit0|px1_bit1|px1_bit0|px2_bit1|px2_bit0|px3_bit1|px3_bit0]
            const size_t src_byte_idx = ((size_t)y * src_stride_bytes) + (x >> 2);
            const uint8_t src_byte = pixels[src_byte_idx];
            const uint8_t shift = (uint8_t)(6U - ((x & 0x3U) * 2U));  // 6, 4, 2, 0
            const uint8_t gray2 = (uint8_t)((src_byte >> shift) & 0x03U);

            // 提取高位和低位
            const uint8_t dtm1_bit = (gray2 >> 1) & 0x01U;  // bit1 → DTM1
            const uint8_t dtm2_bit = gray2 & 0x01U;          // bit0 → DTM2

            // 计算目标位置（1bpp 格式，MSB 在前）
            const uint16_t dst_x = (uint16_t)(x + x_offset);
            const size_t dtm_byte_idx = ((size_t)y * dtm_stride) + (dst_x >> 3);
            const uint8_t dtm_bit_mask = (uint8_t)(0x80U >> (dst_x & 0x7U));
            //
            // 为什么需要取反？
            //
            // DTM register: bit=1=充电(白), bit=0=放电(黑)
            // 两者语义相反，所以要取反
            if (dtm1_bit) {
                (*dtm1)[dtm_byte_idx] &= (uint8_t)(~dtm_bit_mask);  // 取反：1→0
            } else {
                (*dtm1)[dtm_byte_idx] |= dtm_bit_mask;               // 取反：0→1
            }

            // DTM2 同样取反
            if (dtm2_bit) {
                (*dtm2)[dtm_byte_idx] &= (uint8_t)(~dtm_bit_mask);  // 取反：1→0
            } else {
                (*dtm2)[dtm_byte_idx] |= dtm_bit_mask;               // 取反：0→1
            }
        }
    }

    // 如果需要水平镜像
    if (mirror_x) {
        seeed_epaper_panel_mirror_x(*dtm1, aligned_area->width, aligned_area->height);
        seeed_epaper_panel_mirror_x(*dtm2, aligned_area->width, aligned_area->height);
    }

    return ESP_OK;
}

static esp_err_t spi_write_level(seeed_epaper_panel_t *panel, int dc_level, const void *data, size_t len)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");
    ESP_RETURN_ON_FALSE(panel->config.spi_handle != NULL, ESP_ERR_INVALID_STATE, TAG, "spi handle is null");

    if (len == 0U) {
        return ESP_OK;
    }

    if (panel->config.manual_cs && gpio_is_valid(panel->config.pin_cs)) {
        if (dc_level == 0) {
            gpio_set_level(panel->config.pin_cs, 1);
            gpio_set_level(panel->config.pin_cs, 0);
        } else {
            gpio_set_level(panel->config.pin_cs, 0);
        }
    }
    gpio_set_level(panel->config.pin_dc, dc_level);

    const uint8_t *cursor = (const uint8_t *)data;
    size_t remaining = len;
    const uint64_t start_ms = epaper_log_ms();
    while (remaining > 0U) {
        const size_t chunk = remaining > SEEED_EPAPER_SPI_CHUNK_BYTES ? SEEED_EPAPER_SPI_CHUNK_BYTES : remaining;
        ESP_RETURN_ON_ERROR(ensure_spi_tx_buffer(panel), TAG, "ensure spi tx buffer failed");
        memcpy(panel->spi_tx_buffer, cursor, chunk);
        spi_transaction_t trans = {
            .length = chunk * 8U,
            .tx_buffer = panel->spi_tx_buffer,
        };
        esp_err_t err = spi_device_transmit(panel->config.spi_handle, &trans);
        if (err != ESP_OK) {
            APP_EPAPER_DEBUG_LOGE(TAG,
                     "spi transmit failed: dc=%d chunk=%u remaining=%u total=%u elapsed=%llums err=%s",
                     dc_level,
                     (unsigned)chunk,
                     (unsigned)remaining,
                     (unsigned)len,
                     (unsigned long long)(epaper_log_ms() - start_ms),
                     esp_err_to_name(err));
            return err;
        }
        cursor += chunk;
        remaining -= chunk;
    }

    if (len >= SEEED_EPAPER_SPI_CHUNK_BYTES) {
        APP_EPAPER_DEBUG_LOGI(TAG,
                 "spi data transfer done: dc=%d len=%u elapsed=%llums",
                 dc_level,
                 (unsigned)len,
                 (unsigned long long)(epaper_log_ms() - start_ms));
    }
    return ESP_OK;
}

esp_err_t seeed_epaper_panel_write_command(seeed_epaper_panel_t *panel, uint8_t command)
{
    APP_EPAPER_DEBUG_LOGD(TAG, "write command: 0x%02X", command);
    return spi_write_level(panel, 0, &command, sizeof(command));
}

esp_err_t seeed_epaper_panel_write_data(seeed_epaper_panel_t *panel, const void *data, size_t len)
{
    APP_EPAPER_DEBUG_LOGD(TAG, "write data: len=%u", (unsigned)len);
    return spi_write_level(panel, 1, data, len);
}

esp_err_t seeed_epaper_panel_read_data(seeed_epaper_panel_t *panel, uint8_t *data)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");
    ESP_RETURN_ON_FALSE(data != NULL, ESP_ERR_INVALID_ARG, TAG, "data is null");
    ESP_RETURN_ON_FALSE(panel->config.spi_handle != NULL, ESP_ERR_INVALID_STATE, TAG, "spi handle is null");

    if (panel->config.manual_cs && gpio_is_valid(panel->config.pin_cs)) {
        gpio_set_level(panel->config.pin_cs, 1);
        gpio_set_level(panel->config.pin_dc, 1);
        gpio_set_level(panel->config.pin_cs, 0);
    } else {
        gpio_set_level(panel->config.pin_dc, 1);
    }

    spi_transaction_t trans = {
        .flags = SPI_TRANS_USE_RXDATA,
        .rxlength = 8,
    };
    ESP_RETURN_ON_ERROR(spi_device_transmit(panel->config.spi_handle, &trans), TAG, "spi read failed");
    *data = trans.rx_data[0];
    return ESP_OK;
}

esp_err_t seeed_epaper_panel_wait_ready(seeed_epaper_panel_t *panel)
{
    if (!gpio_is_valid(panel->config.pin_busy)) {
        APP_EPAPER_DEBUG_LOGD(TAG, "busy wait skipped: no busy pin");
        return ESP_OK;
    }

    const uint64_t start_ms = epaper_log_ms();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(panel->config.busy_timeout_ms);
    int level = gpio_get_level(panel->config.pin_busy);

    if (level != (int)panel->config.busy_level) {
        APP_EPAPER_DEBUG_LOGD(TAG,
                 "busy ready immediately: pin=%d level=%d busy_level=%u",
                 (int)panel->config.pin_busy,
                 level,
                 (unsigned)panel->config.busy_level);
        return ESP_OK;
    }
    if (panel->busy_sem == NULL || !panel->busy_isr_attached) {
        APP_EPAPER_DEBUG_LOGE(TAG,
                 "busy wait cannot start: sem=%p isr_attached=%d pin=%d level=%d",
                 panel->busy_sem,
                 panel->busy_isr_attached ? 1 : 0,
                 (int)panel->config.pin_busy,
                 level);
        return ESP_ERR_INVALID_STATE;
    }

    while (xSemaphoreTake(panel->busy_sem, 0) == pdTRUE) {
    }

    const gpio_int_type_t done_edge =
        panel->config.busy_level != 0U ? GPIO_INTR_NEGEDGE : GPIO_INTR_POSEDGE;
    APP_EPAPER_DEBUG_LOGI(TAG,
             "busy wait start: pin=%d level=%d busy_level=%u edge=%d timeout=%ums",
             (int)panel->config.pin_busy,
             level,
             (unsigned)panel->config.busy_level,
             (int)done_edge,
             (unsigned)panel->config.busy_timeout_ms);
    atomic_store_explicit(&s_busy_wait_active, true, memory_order_release);
    esp_err_t intr_err = gpio_set_intr_type(panel->config.pin_busy, done_edge);
    if (intr_err != ESP_OK) {
        atomic_store_explicit(&s_busy_wait_active, false, memory_order_release);
        ESP_LOGE(TAG, "enable busy interrupt failed: %s", esp_err_to_name(intr_err));
        return intr_err;
    }

    level = gpio_get_level(panel->config.pin_busy);
    if (level != (int)panel->config.busy_level) {
        (void)gpio_set_intr_type(panel->config.pin_busy, GPIO_INTR_DISABLE);
        APP_EPAPER_DEBUG_LOGI(TAG,
                 "busy wait done before blocking: pin=%d level=%d elapsed=%llums",
                 (int)panel->config.pin_busy,
                 level,
                 (unsigned long long)(epaper_log_ms() - start_ms));
        atomic_store_explicit(&s_busy_wait_active, false, memory_order_release);
        return ESP_OK;
    }

    const BaseType_t ok = xSemaphoreTake(panel->busy_sem, timeout_ticks);
    (void)gpio_set_intr_type(panel->config.pin_busy, GPIO_INTR_DISABLE);
    level = gpio_get_level(panel->config.pin_busy);
    if (ok != pdTRUE) {
        APP_EPAPER_DEBUG_LOGE(TAG,
                 "busy wait timeout: pin=%d level=%d busy_level=%u elapsed=%llums timeout=%ums",
                 (int)panel->config.pin_busy,
                 level,
                 (unsigned)panel->config.busy_level,
                 (unsigned long long)(epaper_log_ms() - start_ms),
                 (unsigned)panel->config.busy_timeout_ms);
        atomic_store_explicit(&s_busy_wait_active, false, memory_order_release);
        return ESP_ERR_TIMEOUT;
    }
    APP_EPAPER_DEBUG_LOGI(TAG,
             "busy wait done: pin=%d level=%d elapsed=%llums",
             (int)panel->config.pin_busy,
             level,
             (unsigned long long)(epaper_log_ms() - start_ms));
    atomic_store_explicit(&s_busy_wait_active, false, memory_order_release);
    return ESP_OK;
}

esp_err_t seeed_epaper_panel_reset(seeed_epaper_panel_t *panel)
{
    panel->refresh_powered = false;
    if (!gpio_is_valid(panel->config.pin_rst)) {
        APP_EPAPER_DEBUG_LOGD(TAG, "reset skipped: no reset pin");
        return ESP_OK;
    }

    const uint64_t start_ms = epaper_log_ms();
    APP_EPAPER_DEBUG_LOGI(TAG,
             "reset start: rst_pin=%d low=%ums high=%ums",
             (int)panel->config.pin_rst,
             (unsigned)panel->config.reset_low_ms,
             (unsigned)panel->config.reset_high_ms);
    gpio_set_level(panel->config.pin_rst, 0);
    vTaskDelay(pdMS_TO_TICKS(panel->config.reset_low_ms));
    gpio_set_level(panel->config.pin_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(panel->config.reset_high_ms));
    APP_EPAPER_DEBUG_LOGI(TAG,
             "reset done: rst_pin=%d elapsed=%llums",
             (int)panel->config.pin_rst,
             (unsigned long long)(epaper_log_ms() - start_ms));
    return ESP_OK;
}

void seeed_epaper_panel_mirror_x(uint8_t *buffer, uint16_t width, uint16_t height)
{
    const size_t stride = packed_mono_stride(width);
    for (uint16_t row = 0; row < height; row++) {
        uint8_t *line = buffer + ((size_t)row * stride);
        for (size_t left = 0, right = stride - 1; left < right; left++, right--) {
            const uint8_t left_value = reverse_bits_u8(line[left]);
            const uint8_t right_value = reverse_bits_u8(line[right]);
            line[left] = right_value;
            line[right] = left_value;
        }
        if ((stride & 0x1U) != 0U) {
            line[stride >> 1] = reverse_bits_u8(line[stride >> 1]);
        }
    }
}

bool seeed_epaper_panel_resolve_area(seeed_epaper_panel_t *panel,
                                     const seeed_epaper_area_t *requested,
                                     seeed_epaper_area_t *resolved)
{
    seeed_epaper_area_t area = {
        .x = 0,
        .y = 0,
        .width = panel->info.width,
        .height = panel->info.height,
    };

    if (requested != NULL) {
        area = *requested;
    }

    if (area.width == 0U || area.height == 0U) {
        return false;
    }
    if (area.x >= panel->info.width || area.y >= panel->info.height) {
        return false;
    }

    const uint32_t max_width = (uint32_t)panel->info.width - area.x;
    const uint32_t max_height = (uint32_t)panel->info.height - area.y;
    if (area.width > max_width) {
        area.width = (uint16_t)max_width;
    }
    if (area.height > max_height) {
        area.height = (uint16_t)max_height;
    }

    *resolved = area;
    return true;
}

void seeed_epaper_panel_set_dirty(seeed_epaper_panel_t *panel, const seeed_epaper_area_t *area)
{
    if (!panel->dirty_valid) {
        panel->dirty_area = *area;
        panel->dirty_valid = true;
        return;
    }

    const uint16_t x1 = panel->dirty_area.x < area->x ? panel->dirty_area.x : area->x;
    const uint16_t y1 = panel->dirty_area.y < area->y ? panel->dirty_area.y : area->y;
    const uint16_t x2a = (uint16_t)(panel->dirty_area.x + panel->dirty_area.width);
    const uint16_t y2a = (uint16_t)(panel->dirty_area.y + panel->dirty_area.height);
    const uint16_t x2b = (uint16_t)(area->x + area->width);
    const uint16_t y2b = (uint16_t)(area->y + area->height);
    const uint16_t x2 = x2a > x2b ? x2a : x2b;
    const uint16_t y2 = y2a > y2b ? y2a : y2b;

    panel->dirty_area.x = x1;
    panel->dirty_area.y = y1;
    panel->dirty_area.width = (uint16_t)(x2 - x1);
    panel->dirty_area.height = (uint16_t)(y2 - y1);
}

static void fill_default_config(seeed_epaper_panel_config_t *config)
{
    if (config->busy_timeout_ms == 0U) {
        config->busy_timeout_ms = 5000U;
    }
    if (config->reset_low_ms == 0U) {
        config->reset_low_ms = 10U;
    }
    if (config->reset_high_ms == 0U) {
        config->reset_high_ms = 10U;
    }
    if (!config->busy_level_valid) {
        config->busy_level = 1U;
    }
    if (!config->enable_level_valid) {
        config->enable_level = 1U;
    }
}

static uint8_t gray4_nibble_to_ssd2677_pixel(uint8_t gray4)
{
    if (gray4 <= 0x03U) {
        switch (gray4) {
        case 0x00U:
            return 0x00U;
        case 0x01U:
            return 0x01U;
        case 0x02U:
            return 0x02U;
        default:
            return 0x03U;
        }
    }

    switch ((gray4 >> 2) & 0x03U) {
    case 0x00U:
        return 0x00U;
    case 0x01U:
        return 0x02U;
    case 0x02U:
        return 0x01U;
    default:
        return 0x03U;
    }
}

static esp_err_t update_ssd2677_gray4_region(seeed_epaper_panel_t *panel,
                                             const seeed_epaper_area_t *area,
                                             const uint8_t *pixels,
                                             size_t src_stride_bytes)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");
    ESP_RETURN_ON_FALSE(area != NULL, ESP_ERR_INVALID_ARG, TAG, "area is null");
    ESP_RETURN_ON_FALSE(pixels != NULL, ESP_ERR_INVALID_ARG, TAG, "pixels is null");
    ESP_RETURN_ON_FALSE(panel->gray4_framebuffer != NULL, ESP_ERR_INVALID_STATE, TAG, "gray4 framebuffer is null");

    const size_t src_stride_2bpp = packed_2bpp_stride(area->width);
    const size_t src_stride_4bpp = pixel_format_stride(area->width, SEEED_EPAPER_PIXEL_FORMAT_GRAY4);
    if (src_stride_bytes == 0U) {
        src_stride_bytes = src_stride_2bpp;
    }
    const bool src_is_2bpp = (src_stride_bytes == src_stride_2bpp);
    const bool src_is_4bpp = (src_stride_bytes == src_stride_4bpp);
    ESP_RETURN_ON_FALSE(src_is_2bpp || src_is_4bpp, ESP_ERR_INVALID_ARG, TAG, "invalid gray4 stride");

    const size_t dst_stride = packed_2bpp_stride(panel->info.width);
    for (uint16_t y = 0; y < area->height; ++y) {
        const uint8_t *src_row = pixels + ((size_t)y * src_stride_bytes);
        for (uint16_t x = 0; x < area->width; ++x) {
            uint8_t gray4 = 0U;
            if (src_is_2bpp) {
                const uint8_t packed = src_row[x >> 2];
                const uint8_t shift = (uint8_t)(6U - ((x & 0x03U) * 2U));
                gray4 = (uint8_t)((packed >> shift) & 0x03U);
            } else {
                const uint8_t packed = src_row[x >> 1];
                gray4 = (x & 0x1U) == 0U ? (uint8_t)(packed >> 4) : (packed & 0x0FU);
            }
            const uint8_t epd2 = gray4_nibble_to_ssd2677_pixel(gray4);
            const uint16_t logical_x = (uint16_t)(area->x + x);
            const uint16_t dst_x = panel->config.mirror_x
                                       ? (uint16_t)(panel->info.width - 1U - logical_x)
                                       : logical_x;
            const size_t dst_index = ((size_t)(area->y + y) * dst_stride) + (dst_x >> 2);
            const uint8_t shift = (uint8_t)(6U - ((dst_x & 0x03U) * 2U));
            panel->gray4_framebuffer[dst_index] =
                (uint8_t)((panel->gray4_framebuffer[dst_index] & (uint8_t)~(0x03U << shift)) |
                          (uint8_t)(epd2 << shift));
        }
    }

    panel->gray4_valid = true;
    return ESP_OK;
}

static esp_err_t configure_gpio(const seeed_epaper_panel_config_t *config)
{
    gpio_config_t io_conf = {0};
    APP_EPAPER_DEBUG_LOGI(TAG,
             "configure gpio: dc=%d cs=%d rst=%d busy=%d enable=%d busy_level=%u enable_level=%u",
             (int)config->pin_dc,
             (int)config->pin_cs,
             (int)config->pin_rst,
             (int)config->pin_busy,
             (int)config->pin_enable,
             (unsigned)config->busy_level,
             (unsigned)config->enable_level);

    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;

    uint64_t output_mask = 0;
    if (gpio_is_valid(config->pin_dc)) {
        output_mask |= 1ULL << config->pin_dc;
    }
    if (config->manual_cs && gpio_is_valid(config->pin_cs)) {
        output_mask |= 1ULL << config->pin_cs;
    }
    if (gpio_is_valid(config->pin_rst)) {
        output_mask |= 1ULL << config->pin_rst;
    }
    if (gpio_is_valid(config->pin_enable)) {
        output_mask |= 1ULL << config->pin_enable;
    }
    if (output_mask != 0U) {
        io_conf.pin_bit_mask = output_mask;
        ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "configure output gpio failed");
    }

    if (gpio_is_valid(config->pin_busy)) {
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pin_bit_mask = 1ULL << config->pin_busy;
        ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "configure busy gpio failed");
    }

    return ESP_OK;
}

static esp_err_t configure_busy_interrupt(seeed_epaper_panel_t *panel)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");
    if (!gpio_is_valid(panel->config.pin_busy)) {
        APP_EPAPER_DEBUG_LOGI(TAG, "busy interrupt skipped: no busy pin");
        return ESP_OK;
    }

    panel->busy_sem = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(panel->busy_sem != NULL, ESP_ERR_NO_MEM, TAG, "create busy semaphore failed");

    esp_err_t err = gpio_isr_handler_add(panel->config.pin_busy, busy_gpio_isr_handler, panel);
    if (err == ESP_ERR_INVALID_STATE) {
        if (!ensure_gpio_isr_service()) {
            vSemaphoreDelete(panel->busy_sem);
            panel->busy_sem = NULL;
            return ESP_FAIL;
        }
        err = gpio_isr_handler_add(panel->config.pin_busy, busy_gpio_isr_handler, panel);
    }
    if (err != ESP_OK) {
        vSemaphoreDelete(panel->busy_sem);
        panel->busy_sem = NULL;
        APP_EPAPER_DEBUG_LOGW(TAG, "add busy GPIO ISR failed: %s", esp_err_to_name(err));
        return err;
    }

    panel->busy_isr_attached = true;
    (void)gpio_set_intr_type(panel->config.pin_busy, GPIO_INTR_DISABLE);
    APP_EPAPER_DEBUG_LOGI(TAG,
             "busy interrupt attached: pin=%d idle_level=%d busy_level=%u timeout=%ums",
             (int)panel->config.pin_busy,
             gpio_get_level(panel->config.pin_busy),
             (unsigned)panel->config.busy_level,
             (unsigned)panel->config.busy_timeout_ms);
    return ESP_OK;
}

static const seeed_epaper_driver_t *select_driver(seeed_epaper_panel_model_t model)
{
    switch (model) {
    case SEEED_EPAPER_PANEL_UC8179:
        return seeed_epaper_uc8179_driver();
    case SEEED_EPAPER_PANEL_SSD1677:
        return seeed_epaper_ssd1677_driver();
    case SEEED_EPAPER_PANEL_SSD2677:
        return seeed_epaper_ssd2677_driver();
    default:
        return NULL;
    }
}

static bool is_ssd_family(seeed_epaper_panel_model_t model)
{
    return model == SEEED_EPAPER_PANEL_SSD1677 || model == SEEED_EPAPER_PANEL_SSD2677;
}

static void apply_panel_model_config(seeed_epaper_panel_t *panel, seeed_epaper_panel_model_t model)
{
    if (model == SEEED_EPAPER_PANEL_SSD2677) {
        panel->config.busy_timeout_ms = kSsd2677BusyTimeoutMs;
        panel->config.reset_low_ms = kSsd2677ResetLowMs;
        panel->config.reset_high_ms = kSsd2677ResetHighMs;
        panel->config.busy_level = 0U;
        return;
    }

    if (model == SEEED_EPAPER_PANEL_SSD1677) {
        panel->config.busy_timeout_ms = kSsd1677BusyTimeoutMs;
        panel->config.reset_low_ms = kSsd1677ResetLowMs;
        panel->config.reset_high_ms = kSsd1677ResetHighMs;
        panel->config.busy_level = 1U;
    }
}

static esp_err_t resolve_panel_model(seeed_epaper_panel_t *panel, seeed_epaper_panel_model_t requested_model)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");

    seeed_epaper_panel_model_t resolved_model = requested_model;
    if (is_ssd_family(requested_model)) {
        uint8_t chip_id = 0U;

        apply_panel_model_config(panel, SEEED_EPAPER_PANEL_SSD2677);
        ESP_RETURN_ON_ERROR(seeed_epaper_panel_reset(panel), TAG, "probe reset failed");
        ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, 0x70), TAG, "probe id command failed");
        ESP_RETURN_ON_ERROR(seeed_epaper_panel_read_data(panel, &chip_id), TAG, "probe id read failed");

        resolved_model = chip_id == kSsd2677ChipId ? SEEED_EPAPER_PANEL_SSD2677 : SEEED_EPAPER_PANEL_SSD1677;
        // Keep controller detection visible even when timing traces are disabled.
        // 即使关闭高频时序日志，也保留芯片识别结果用于硬件诊断。
        ESP_LOGI(TAG,
                 "panel probe: requested=%d chip_id=0x%02X resolved=%d",
                 (int)requested_model,
                 chip_id,
                 (int)resolved_model);
    }

    apply_panel_model_config(panel, resolved_model);
    panel->model = resolved_model;
    panel->driver = select_driver(resolved_model);
    panel->info = panel->driver->info;
    return ESP_OK;
}

esp_err_t seeed_epaper_new_panel(seeed_epaper_panel_model_t model,
                                 const seeed_epaper_panel_config_t *config,
                                 seeed_epaper_panel_handle_t *ret_panel)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is null");
    ESP_RETURN_ON_FALSE(ret_panel != NULL, ESP_ERR_INVALID_ARG, TAG, "ret_panel is null");
    *ret_panel = NULL;
    ESP_RETURN_ON_FALSE(config->spi_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "spi handle is null");
    ESP_RETURN_ON_FALSE(gpio_is_valid(config->pin_dc), ESP_ERR_INVALID_ARG, TAG, "pin_dc is invalid");

    ESP_RETURN_ON_FALSE(select_driver(model) != NULL, ESP_ERR_NOT_SUPPORTED, TAG, "panel model not supported");

    APP_EPAPER_DEBUG_LOGI(TAG,
             "new panel start: model=%d spi_handle=%p dc=%d cs=%d manual_cs=%d rst=%d busy=%d enable=%d busy_timeout=%ums mirror_x=%d",
             (int)model,
             (void *)config->spi_handle,
             (int)config->pin_dc,
             (int)config->pin_cs,
             config->manual_cs ? 1 : 0,
             (int)config->pin_rst,
             (int)config->pin_busy,
             (int)config->pin_enable,
             (unsigned)config->busy_timeout_ms,
             config->mirror_x ? 1 : 0);

    seeed_epaper_panel_t *panel = (seeed_epaper_panel_t *)calloc(1, sizeof(seeed_epaper_panel_t));
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_NO_MEM, TAG, "alloc panel failed");

    panel->model = model;
    panel->config = *config;
    fill_default_config(&panel->config);
    panel->driver = NULL;
    panel->info = (seeed_epaper_panel_info_t){0};
    panel->sleeping = true;
    panel->standby = false;

    esp_err_t err = configure_gpio(&panel->config);
    if (err != ESP_OK) {
        free(panel);
        return err;
    }

    err = configure_busy_interrupt(panel);
    if (err != ESP_OK) {
        free(panel);
        return err;
    }

    if (gpio_is_valid(panel->config.pin_enable)) {
        gpio_set_level(panel->config.pin_enable, panel->config.enable_level);
    }
    if (panel->config.manual_cs && gpio_is_valid(panel->config.pin_cs)) {
        gpio_set_level(panel->config.pin_cs, 1);
    }
    if (gpio_is_valid(panel->config.pin_rst)) {
        gpio_set_level(panel->config.pin_rst, 1);
    }

    err = resolve_panel_model(panel, model);
    if (err != ESP_OK) {
        (void)seeed_epaper_panel_del(panel);
        return err;
    }

    err = ensure_framebuffer(panel);
    if (err != ESP_OK) {
        (void)seeed_epaper_panel_del(panel);
        return err;
    }

    const seeed_epaper_driver_t *driver = panel->driver;
    *ret_panel = panel;
    APP_EPAPER_DEBUG_LOGI(TAG,
             "new panel done: model=%d driver=%s size=%ux%u framebuffer=%u busy_level=%u timeout=%ums",
             (int)panel->model,
             driver->name,
             (unsigned)panel->info.width,
             (unsigned)panel->info.height,
             (unsigned)panel->framebuffer_len,
             (unsigned)panel->config.busy_level,
             (unsigned)panel->config.busy_timeout_ms);
    return ESP_OK;
}

esp_err_t seeed_epaper_panel_del(seeed_epaper_panel_handle_t panel)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");
    if (panel->busy_isr_attached && gpio_is_valid(panel->config.pin_busy)) {
        (void)gpio_set_intr_type(panel->config.pin_busy, GPIO_INTR_DISABLE);
        (void)gpio_isr_handler_remove(panel->config.pin_busy);
        panel->busy_isr_attached = false;
    }
    if (panel->busy_sem != NULL) {
        vSemaphoreDelete(panel->busy_sem);
        panel->busy_sem = NULL;
    }
    if (panel->framebuffer != NULL) {
        heap_caps_free(panel->framebuffer);
        panel->framebuffer = NULL;
    }
    if (panel->previous_framebuffer != NULL) {
        heap_caps_free(panel->previous_framebuffer);
        panel->previous_framebuffer = NULL;
    }
    if (panel->gray4_framebuffer != NULL) {
        heap_caps_free(panel->gray4_framebuffer);
        panel->gray4_framebuffer = NULL;
    }
    if (panel->spi_tx_buffer != NULL) {
        heap_caps_free(panel->spi_tx_buffer);
        panel->spi_tx_buffer = NULL;
    }
    free(panel);
    return ESP_OK;
}

esp_err_t seeed_epaper_panel_get_info(seeed_epaper_panel_handle_t panel,
                                      seeed_epaper_panel_info_t *ret_info)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");
    ESP_RETURN_ON_FALSE(ret_info != NULL, ESP_ERR_INVALID_ARG, TAG, "ret_info is null");
    *ret_info = panel->info;
    return ESP_OK;
}

esp_err_t seeed_epaper_panel_get_model(seeed_epaper_panel_handle_t panel,
                                       seeed_epaper_panel_model_t *ret_model)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");
    ESP_RETURN_ON_FALSE(ret_model != NULL, ESP_ERR_INVALID_ARG, TAG, "ret_model is null");
    *ret_model = panel->model;
    return ESP_OK;
}

esp_err_t seeed_epaper_panel_get_last_refresh_timing(seeed_epaper_panel_handle_t panel,
                                                      seeed_epaper_refresh_timing_t *ret_timing)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");
    ESP_RETURN_ON_FALSE(ret_timing != NULL, ESP_ERR_INVALID_ARG, TAG, "ret_timing is null");
    *ret_timing = panel->last_refresh_timing;
    return ESP_OK;
}

esp_err_t seeed_epaper_panel_set_external_temperature(seeed_epaper_panel_handle_t panel,
                                                      float temperature_c)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");
    panel->external_temperature_c = temperature_c;
    panel->external_temperature_valid = true;
    // ESP_LOGI(TAG, "external temperature set: %.1fC", (double)temperature_c);
    return ESP_OK;
}

esp_err_t seeed_epaper_panel_clear_external_temperature(seeed_epaper_panel_handle_t panel)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");
    panel->external_temperature_valid = false;
    panel->external_temperature_c = 0.0f;
    // ESP_LOGI(TAG, "external temperature cleared");
    return ESP_OK;
}

esp_err_t seeed_epaper_panel_read_register8(seeed_epaper_panel_handle_t panel,
                                            uint8_t command,
                                            uint8_t *ret_value)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");
    ESP_RETURN_ON_FALSE(ret_value != NULL, ESP_ERR_INVALID_ARG, TAG, "ret_value is null");

    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_command(panel, command), TAG, "write read command failed");
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_wait_ready(panel), TAG, "wait read command failed");
    return seeed_epaper_panel_read_data(panel, ret_value);
}

esp_err_t seeed_epaper_panel_prepare(seeed_epaper_panel_handle_t panel,
                                     seeed_epaper_refresh_mode_t mode)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");
    const uint64_t start_ms = epaper_log_ms();
    const seeed_epaper_refresh_mode_t effective_mode = resolve_refresh_mode(panel, mode);
    APP_EPAPER_DEBUG_LOGI(TAG,
             "prepare start: requested=%s effective=%s prepared=%d sleeping=%d standby=%d active=%s partial_ready=%d",
             refresh_mode_name(mode),
             refresh_mode_name(effective_mode),
             panel->prepared ? 1 : 0,
             panel->sleeping ? 1 : 0,
             panel->standby ? 1 : 0,
             refresh_mode_name(panel->active_mode),
             panel->partial_ready ? 1 : 0);
    if (panel->standby && !panel->sleeping && panel->active_mode == effective_mode) {
        ESP_RETURN_ON_FALSE(panel->driver->resume != NULL, ESP_ERR_NOT_SUPPORTED, TAG, "driver resume not supported");
        esp_err_t err = panel->driver->resume(panel);
        if (err != ESP_OK) {
            mark_panel_needs_reinit(panel);
            APP_EPAPER_DEBUG_LOGE(TAG, "driver resume failed: %s", esp_err_to_name(err));
            return err;
        }
        panel->standby = false;
        panel->prepared = true;
        panel->sleeping = false;
        APP_EPAPER_DEBUG_LOGI(TAG,
                 "[perf] prepare resume mode=%s total=%llums",
                 refresh_mode_name(effective_mode),
                 (unsigned long long)(epaper_log_ms() - start_ms));
        APP_EPAPER_DEBUG_LOGI(TAG,
                 "prepare resumed from standby: mode=%s elapsed=%llums",
                 refresh_mode_name(effective_mode),
                 (unsigned long long)(epaper_log_ms() - start_ms));
        return ESP_OK;
    }
    if (panel->prepared && panel->active_mode == effective_mode) {
        APP_EPAPER_DEBUG_LOGI(TAG,
                 "[perf] prepare skip mode=%s total=%llums",
                 refresh_mode_name(effective_mode),
                 (unsigned long long)(epaper_log_ms() - start_ms));
        APP_EPAPER_DEBUG_LOGI(TAG,
                 "prepare skip: already prepared mode=%s elapsed=%llums",
                 refresh_mode_name(effective_mode),
                 (unsigned long long)(epaper_log_ms() - start_ms));
        return ESP_OK;
    }

    esp_err_t err = panel->driver->init_mode(panel, effective_mode);
    if (err != ESP_OK) {
        mark_panel_needs_reinit(panel);
        APP_EPAPER_DEBUG_LOGE(TAG, "driver init failed: %s", esp_err_to_name(err));
        return err;
    }

    panel->prepared = true;
    panel->sleeping = false;
    panel->standby = false;
    panel->partial_after_standby = false;
    panel->active_mode = effective_mode;
    if (effective_mode == SEEED_EPAPER_REFRESH_GRAY4 &&
        panel->model == SEEED_EPAPER_PANEL_SSD1677) {
        // Gray4 overwrites both controller planes but has no mono baseline.
        // 四级灰度覆盖两路显存，下一次局刷必须重新建立黑白基线。
        panel->partial_ready = false;
    }
    if (effective_mode == SEEED_EPAPER_REFRESH_FULL) {
        panel->dirty_area.x = 0;
        panel->dirty_area.y = 0;
        panel->dirty_area.width = panel->info.width;
        panel->dirty_area.height = panel->info.height;
        panel->dirty_valid = true;
    }
    APP_EPAPER_DEBUG_LOGI(TAG,
             "[perf] prepare init mode=%s total=%llums",
             refresh_mode_name(effective_mode),
             (unsigned long long)(epaper_log_ms() - start_ms));
    APP_EPAPER_DEBUG_LOGI(TAG,
             "prepare done: mode=%s elapsed=%llums",
             refresh_mode_name(effective_mode),
             (unsigned long long)(epaper_log_ms() - start_ms));
    return ESP_OK;
}

esp_err_t seeed_epaper_panel_wakeup(seeed_epaper_panel_handle_t panel,
                                    seeed_epaper_refresh_mode_t mode)
{
    APP_EPAPER_DEBUG_LOGI(TAG, "wakeup requested: mode=%s", refresh_mode_name(mode));
    return seeed_epaper_panel_prepare(panel, mode);
}

esp_err_t seeed_epaper_panel_write_bitmap(seeed_epaper_panel_handle_t panel,
                                          const seeed_epaper_area_t *area,
                                          const void *pixels,
                                          size_t src_stride_bytes,
                                          seeed_epaper_pixel_format_t input_format,
                                          seeed_epaper_refresh_mode_t mode)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");
    ESP_RETURN_ON_FALSE(area != NULL, ESP_ERR_INVALID_ARG, TAG, "area is null");
    ESP_RETURN_ON_FALSE(pixels != NULL, ESP_ERR_INVALID_ARG, TAG, "pixels is null");

    const uint64_t start_ms = epaper_log_ms();
    uint64_t phase_start_ms;
    APP_EPAPER_DEBUG_LOGI(TAG,
             "write_bitmap start: mode=%s format=%s src_stride=%u",
             refresh_mode_name(mode),
             pixel_format_name(input_format),
             (unsigned)src_stride_bytes);
    log_area("write_bitmap requested", area);

    seeed_epaper_area_t clipped;
    ESP_RETURN_ON_FALSE(seeed_epaper_panel_resolve_area(panel, area, &clipped), ESP_ERR_INVALID_ARG, TAG, "invalid area");

    seeed_epaper_area_t aligned = clipped;
    const uint16_t x1 = (uint16_t)(aligned.x & ~0x7U);
    const uint16_t x2 = (uint16_t)((aligned.x + aligned.width + 7U) & ~0x7U);
    aligned.x = x1;
    aligned.width = (uint16_t)(x2 - x1);
    log_area("write_bitmap clipped", &clipped);
    log_area("write_bitmap aligned", &aligned);

    phase_start_ms = epaper_log_ms();
    esp_err_t err = seeed_epaper_panel_prepare(panel, mode);
    if (err != ESP_OK) {
        APP_EPAPER_DEBUG_LOGE(TAG,
                 "write_bitmap prepare failed: mode=%s elapsed=%llums err=%s",
                 refresh_mode_name(mode),
                 (unsigned long long)(epaper_log_ms() - start_ms),
                 esp_err_to_name(err));
        return err;
    }
    const seeed_epaper_refresh_mode_t effective_mode = panel->active_mode;
    APP_EPAPER_DEBUG_LOGI(TAG,
             "[perf] write_bitmap prepare=%llums mode=%s",
             (unsigned long long)(epaper_log_ms() - phase_start_ms),
             refresh_mode_name(effective_mode));

    phase_start_ms = epaper_log_ms();
    ESP_RETURN_ON_ERROR(ensure_framebuffer(panel), TAG, "ensure framebuffer failed");

    uint8_t *native = NULL;
    size_t native_len = 0;
    err = build_native_1bpp(&clipped,
                            &aligned,
                            pixels,
                            src_stride_bytes,
                            input_format,
                            panel->config.mirror_x,
                            &native,
                            &native_len);
    ESP_RETURN_ON_ERROR(err, TAG, "build native buffer failed");
    APP_EPAPER_DEBUG_LOGI(TAG,
             "write_bitmap native buffer ready: len=%u effective_mode=%s mirror_x=%d",
             (unsigned)native_len,
             refresh_mode_name(effective_mode),
             panel->config.mirror_x ? 1 : 0);
    APP_EPAPER_DEBUG_LOGI(TAG,
             "[perf] write_bitmap framebuffer+convert=%llums native=%uB",
             (unsigned long long)(epaper_log_ms() - phase_start_ms),
             (unsigned)native_len);

    phase_start_ms = epaper_log_ms();
    seeed_epaper_area_t io_area = transform_area_for_io(panel, &aligned);
    log_area("write_bitmap io", &io_area);
    err = panel->driver->set_window(panel, &io_area);
    if (err == ESP_OK) {
        if (effective_mode == SEEED_EPAPER_REFRESH_FULL) {
            err = panel->driver->write_image(panel, native, native_len, true);
            if (err == ESP_OK) {
                err = panel->driver->write_image(panel, native, native_len, false);
            }
        } else if (panel->model == SEEED_EPAPER_PANEL_SSD2677) {
            // SSD2677 streams the complete previous/current framebuffer pair at commit.
            err = panel->driver->write_image(panel, native, native_len, false);
        } else {
            uint8_t *previous = (uint8_t *)alloc_temp_buffer(native_len);
            if (previous == NULL) {
                err = ESP_ERR_NO_MEM;
                APP_EPAPER_DEBUG_LOGE(TAG, "alloc previous buffer failed");
            } else {
                err = copy_framebuffer_region(panel, &io_area, previous, native_len);
                if (err == ESP_OK) {
                    err = panel->driver->write_image(panel, previous, native_len, true);
                }
                if (err == ESP_OK) {
                    err = panel->driver->write_image(panel, native, native_len, false);
                }
                heap_caps_free(previous);
            }
        }
        if (err == ESP_OK) {
            err = update_framebuffer_region(panel, &io_area, native, native_len);
        }
    }
    APP_EPAPER_DEBUG_LOGI(TAG,
             "[perf] write_bitmap stage+framebuffer=%llums",
             (unsigned long long)(epaper_log_ms() - phase_start_ms));
    heap_caps_free(native);
    if (err != ESP_OK) {
        mark_panel_needs_reinit(panel);
        APP_EPAPER_DEBUG_LOGE(TAG, "write image failed: %s", esp_err_to_name(err));
        return err;
    }

    seeed_epaper_panel_set_dirty(panel, &clipped);
    APP_EPAPER_DEBUG_LOGI(TAG,
             "[perf] write_bitmap done: mode=%s total=%llums",
             refresh_mode_name(effective_mode),
             (unsigned long long)(epaper_log_ms() - start_ms));
    APP_EPAPER_DEBUG_LOGI(TAG,
             "write_bitmap done: mode=%s dirty_valid=%d elapsed=%llums",
             refresh_mode_name(effective_mode),
             panel->dirty_valid ? 1 : 0,
             (unsigned long long)(epaper_log_ms() - start_ms));
    return ESP_OK;
}

esp_err_t seeed_epaper_panel_write_bitmap_diff(seeed_epaper_panel_handle_t panel,
                                               const seeed_epaper_area_t *area,
                                               const void *previous_pixels,
                                               const void *current_pixels,
                                               size_t src_stride_bytes,
                                               seeed_epaper_pixel_format_t input_format,
                                               seeed_epaper_refresh_mode_t mode)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");
    ESP_RETURN_ON_FALSE(area != NULL, ESP_ERR_INVALID_ARG, TAG, "area is null");
    ESP_RETURN_ON_FALSE(previous_pixels != NULL, ESP_ERR_INVALID_ARG, TAG, "previous pixels is null");
    ESP_RETURN_ON_FALSE(current_pixels != NULL, ESP_ERR_INVALID_ARG, TAG, "current pixels is null");

    const uint64_t start_ms = epaper_log_ms();
    uint64_t phase_start_ms;
    APP_EPAPER_DEBUG_LOGI(TAG,
             "write_bitmap_diff start: mode=%s format=%s src_stride=%u",
             refresh_mode_name(mode),
             pixel_format_name(input_format),
             (unsigned)src_stride_bytes);
    log_area("write_bitmap_diff requested", area);

    seeed_epaper_area_t clipped;
    ESP_RETURN_ON_FALSE(seeed_epaper_panel_resolve_area(panel, area, &clipped), ESP_ERR_INVALID_ARG, TAG, "invalid area");

    seeed_epaper_area_t aligned = clipped;
    const uint16_t x1 = (uint16_t)(aligned.x & ~0x7U);
    const uint16_t x2 = (uint16_t)((aligned.x + aligned.width + 7U) & ~0x7U);
    aligned.x = x1;
    aligned.width = (uint16_t)(x2 - x1);
    log_area("write_bitmap_diff clipped", &clipped);
    log_area("write_bitmap_diff aligned", &aligned);

    phase_start_ms = epaper_log_ms();
    esp_err_t err = seeed_epaper_panel_prepare(panel, mode);
    if (err != ESP_OK) {
        APP_EPAPER_DEBUG_LOGE(TAG,
                 "write_bitmap_diff prepare failed: mode=%s elapsed=%llums err=%s",
                 refresh_mode_name(mode),
                 (unsigned long long)(epaper_log_ms() - start_ms),
                 esp_err_to_name(err));
        return err;
    }
    const seeed_epaper_refresh_mode_t effective_mode = panel->active_mode;
    APP_EPAPER_DEBUG_LOGI(TAG,
             "[perf] write_diff prepare=%llums mode=%s",
             (unsigned long long)(epaper_log_ms() - phase_start_ms),
             refresh_mode_name(effective_mode));

    phase_start_ms = epaper_log_ms();
    ESP_RETURN_ON_ERROR(ensure_framebuffer(panel), TAG, "ensure framebuffer failed");

    uint8_t *previous_native = NULL;
    size_t previous_len = 0;
    err = build_native_1bpp(&clipped,
                            &aligned,
                            previous_pixels,
                            src_stride_bytes,
                            input_format,
                            panel->config.mirror_x,
                            &previous_native,
                            &previous_len);
    ESP_RETURN_ON_ERROR(err, TAG, "build previous native buffer failed");

    uint8_t *current_native = NULL;
    size_t current_len = 0;
    err = build_native_1bpp(&clipped,
                            &aligned,
                            current_pixels,
                            src_stride_bytes,
                            input_format,
                            panel->config.mirror_x,
                            &current_native,
                            &current_len);
    if (err != ESP_OK) {
        heap_caps_free(previous_native);
        return err;
    }
    APP_EPAPER_DEBUG_LOGI(TAG,
             "write_bitmap_diff native buffers ready: previous=%u current=%u effective_mode=%s mirror_x=%d",
             (unsigned)previous_len,
             (unsigned)current_len,
             refresh_mode_name(effective_mode),
             panel->config.mirror_x ? 1 : 0);
    APP_EPAPER_DEBUG_LOGI(TAG,
             "[perf] write_diff framebuffer+convert=%llums previous=%uB current=%uB",
             (unsigned long long)(epaper_log_ms() - phase_start_ms),
             (unsigned)previous_len,
             (unsigned)current_len);

    if (previous_len != current_len) {
        err = ESP_ERR_INVALID_SIZE;
        APP_EPAPER_DEBUG_LOGE(TAG, "diff buffer size mismatch");
        goto cleanup;
    }

    phase_start_ms = epaper_log_ms();
    {
        seeed_epaper_area_t io_area = transform_area_for_io(panel, &aligned);
        log_area("write_bitmap_diff io", &io_area);
        err = panel->driver->set_window(panel, &io_area);
        if (err == ESP_OK) {
            if (effective_mode == SEEED_EPAPER_REFRESH_FULL) {
                err = panel->driver->write_image(panel, current_native, current_len, true);
                if (err == ESP_OK) {
                    err = panel->driver->write_image(panel, current_native, current_len, false);
                }
            } else if (panel->model == SEEED_EPAPER_PANEL_SSD2677) {
                err = panel->driver->write_image(panel, current_native, current_len, false);
            } else {
                    err = panel->driver->write_image(panel, previous_native, previous_len, true);
                if (err == ESP_OK) {
                    err = panel->driver->write_image(panel, current_native, current_len, false);
                }
            }
            if (err == ESP_OK) {
                err = update_framebuffer_region(panel, &io_area, current_native, current_len);
            }
        }
    }
    APP_EPAPER_DEBUG_LOGI(TAG,
             "[perf] write_diff stage+framebuffer=%llums",
             (unsigned long long)(epaper_log_ms() - phase_start_ms));

cleanup:
    heap_caps_free(previous_native);
    heap_caps_free(current_native);
    if (err != ESP_OK) {
        mark_panel_needs_reinit(panel);
        APP_EPAPER_DEBUG_LOGE(TAG, "write diff image failed: %s", esp_err_to_name(err));
        return err;
    }

    seeed_epaper_panel_set_dirty(panel, &clipped);
    APP_EPAPER_DEBUG_LOGI(TAG,
             "[perf] write_diff done: mode=%s total=%llums",
             refresh_mode_name(effective_mode),
             (unsigned long long)(epaper_log_ms() - start_ms));
    APP_EPAPER_DEBUG_LOGI(TAG,
             "write_bitmap_diff done: mode=%s dirty_valid=%d elapsed=%llums",
             refresh_mode_name(effective_mode),
             panel->dirty_valid ? 1 : 0,
             (unsigned long long)(epaper_log_ms() - start_ms));
    return ESP_OK;
}

//4_gray_surpport: write 2bpp gray4 data to panel (DTM1 to 0x24, DTM2 to 0x26)
esp_err_t seeed_epaper_panel_write_bitmap_gray4(
    seeed_epaper_panel_handle_t panel,
    const seeed_epaper_area_t *area,
    const uint8_t *pixels,
    size_t src_stride_bytes)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");
    ESP_RETURN_ON_FALSE(area != NULL, ESP_ERR_INVALID_ARG, TAG, "area is null");
    ESP_RETURN_ON_FALSE(pixels != NULL, ESP_ERR_INVALID_ARG, TAG, "pixels is null");
    ESP_RETURN_ON_FALSE(panel->info.supports_gray4, ESP_ERR_NOT_SUPPORTED, TAG, "panel does not support gray4");

    // 裁剪到屏幕范围
    seeed_epaper_area_t clipped;
    ESP_RETURN_ON_FALSE(seeed_epaper_panel_resolve_area(panel, area, &clipped),
                        ESP_ERR_INVALID_ARG, TAG, "invalid area");

    // 8 像素对齐
    if (panel->model == SEEED_EPAPER_PANEL_SSD2677) {
        const uint64_t start_ms = epaper_log_ms();
        uint64_t phase_start_ms;
        size_t gray4_stride = src_stride_bytes;
        if (gray4_stride == 0U) {
            gray4_stride = packed_2bpp_stride(clipped.width);
        }

        phase_start_ms = epaper_log_ms();
        esp_err_t err = seeed_epaper_panel_prepare(panel, SEEED_EPAPER_REFRESH_GRAY4);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "prepare SSD2677 GRAY4 failed: %s", esp_err_to_name(err));
            return err;
        }

        APP_EPAPER_DEBUG_LOGI(TAG,
                 "[perf] gray4 prepare=%llums",
                 (unsigned long long)(epaper_log_ms() - phase_start_ms));

        phase_start_ms = epaper_log_ms();
        err = ensure_gray4_framebuffer(panel);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ensure SSD2677 gray4 framebuffer failed: %s", esp_err_to_name(err));
            return err;
        }

        APP_EPAPER_DEBUG_LOGI(TAG,
                 "[perf] gray4 ensure-framebuffer=%llums size=%uB",
                 (unsigned long long)(epaper_log_ms() - phase_start_ms),
                 (unsigned)panel->gray4_framebuffer_len);

        phase_start_ms = epaper_log_ms();
        err = update_ssd2677_gray4_region(panel, &clipped, pixels, gray4_stride);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "update SSD2677 gray4 region failed: %s", esp_err_to_name(err));
            return err;
        }

        APP_EPAPER_DEBUG_LOGI(TAG,
                 "[perf] gray4 update-gray-buffer=%llums",
                 (unsigned long long)(epaper_log_ms() - phase_start_ms));

        phase_start_ms = epaper_log_ms();
        err = ensure_framebuffer(panel);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ensure mono framebuffer for SSD2677 gray4 failed: %s", esp_err_to_name(err));
            return err;
        }

        err = update_framebuffer_region_from_gray4(panel, &clipped, pixels, gray4_stride);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "update mono framebuffer from SSD2677 gray4 failed: %s", esp_err_to_name(err));
            return err;
        }

        APP_EPAPER_DEBUG_LOGI(TAG,
                 "[perf] gray4 update-mono-shadow=%llums",
                 (unsigned long long)(epaper_log_ms() - phase_start_ms));

        panel->dirty_area.x = 0;
        panel->dirty_area.y = 0;
        panel->dirty_area.width = panel->info.width;
        panel->dirty_area.height = panel->info.height;
        panel->dirty_valid = true;
        APP_EPAPER_DEBUG_LOGI(TAG,
                 "[perf] gray4 stage done: total=%llums",
                 (unsigned long long)(epaper_log_ms() - start_ms));
        return ESP_OK;
    }

    seeed_epaper_area_t aligned = clipped;
    const uint16_t x1 = (uint16_t)(aligned.x & ~0x07U);
    const uint16_t x2 = (uint16_t)((aligned.x + aligned.width + 7U) & ~0x07U);
    aligned.x = x1;
    aligned.width = (uint16_t)(x2 - x1);

    // 准备灰度模式
    esp_err_t err = seeed_epaper_panel_prepare(panel, SEEED_EPAPER_REFRESH_GRAY4);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "prepare GRAY4 failed: %s", esp_err_to_name(err));
        return err;
    }

    // 生成 DTM1/DTM2
    uint8_t *dtm1 = NULL;
    uint8_t *dtm2 = NULL;
    size_t dtm1_len = 0;
    size_t dtm2_len = 0;
    err = build_native_gray4_planes(&clipped, &aligned, pixels, src_stride_bytes,
                                    panel->config.mirror_x,
                                    &dtm1, &dtm1_len, &dtm2, &dtm2_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "build_native_gray4_planes failed: %s", esp_err_to_name(err));
        return err;
    }

    // 设置窗口为全屏
    seeed_epaper_area_t full_area = {0, 0, panel->info.width, panel->info.height};
    seeed_epaper_area_t io_area = transform_area_for_io(panel, &full_area);
    err = panel->driver->set_window(panel, &io_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_window failed: %s", esp_err_to_name(err));
        heap_caps_free(dtm1);
        heap_caps_free(dtm2);
        return err;
    }

    // 写 DTM1 到 0x24
    err = panel->driver->write_image(panel, dtm1, dtm1_len, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "write DTM1 to 0x24 failed: %s", esp_err_to_name(err));
        heap_caps_free(dtm1);
        heap_caps_free(dtm2);
        return err;
    }

    // 写 DTM2 到 0x26
    err = panel->driver->write_image(panel, dtm2, dtm2_len, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "write DTM2 to 0x26 failed: %s", esp_err_to_name(err));
        heap_caps_free(dtm1);
        heap_caps_free(dtm2);
        return err;
    }

    heap_caps_free(dtm1);
    heap_caps_free(dtm2);

    // 注意：不调用 update_framebuffer_region（灰度数据格式不兼容）
    return ESP_OK;
}

esp_err_t seeed_epaper_panel_commit(seeed_epaper_panel_handle_t panel,
                                    const seeed_epaper_area_t *area,
                                    seeed_epaper_refresh_mode_t mode)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");
    const uint64_t start_ms = epaper_log_ms();
    uint64_t phase_start_ms;
    APP_EPAPER_DEBUG_LOGI(TAG,
             "commit start: requested_mode=%s dirty_valid=%d partial_ready=%d",
             refresh_mode_name(mode),
             panel->dirty_valid ? 1 : 0,
             panel->partial_ready ? 1 : 0);
    log_area("commit requested", area);

    phase_start_ms = epaper_log_ms();
    esp_err_t err = seeed_epaper_panel_prepare(panel, mode);
    if (err != ESP_OK) {
        APP_EPAPER_DEBUG_LOGE(TAG,
                 "commit prepare failed: mode=%s elapsed=%llums err=%s",
                 refresh_mode_name(mode),
                 (unsigned long long)(epaper_log_ms() - start_ms),
                 esp_err_to_name(err));
        return err;
    }
    const seeed_epaper_refresh_mode_t effective_mode = panel->active_mode;
    APP_EPAPER_DEBUG_LOGI(TAG,
             "[perf] commit prepare=%llums mode=%s",
             (unsigned long long)(epaper_log_ms() - phase_start_ms),
             refresh_mode_name(effective_mode));
    bool sync_partial_previous = false;
    seeed_epaper_area_t partial_io_area = {};

    if (effective_mode == SEEED_EPAPER_REFRESH_PARTIAL) {
        seeed_epaper_area_t refresh_area;
        if (area != NULL) {
            ESP_RETURN_ON_FALSE(seeed_epaper_panel_resolve_area(panel, area, &refresh_area), ESP_ERR_INVALID_ARG, TAG, "invalid area");
        } else if (panel->dirty_valid) {
            refresh_area = panel->dirty_area;
        } else {
            APP_EPAPER_DEBUG_LOGI(TAG,
                     "commit skip: partial mode but no dirty area elapsed=%llums",
                     (unsigned long long)(epaper_log_ms() - start_ms));
            return ESP_OK;
        }
        panel->dirty_area = refresh_area;
        panel->dirty_valid = true;
        const uint16_t x1 = (uint16_t)(refresh_area.x & ~0x7U);
        const uint16_t x2 = (uint16_t)((refresh_area.x + refresh_area.width + 7U) & ~0x7U);
        refresh_area.x = x1;
        refresh_area.width = (uint16_t)(x2 - x1);
        const seeed_epaper_area_t io_area = transform_area_for_io(panel, &refresh_area);
        log_area("commit partial refresh", &refresh_area);
        log_area("commit partial io", &io_area);
        err = panel->driver->set_window(panel, &io_area);
        if (err != ESP_OK) {
            mark_panel_needs_reinit(panel);
            APP_EPAPER_DEBUG_LOGE(TAG, "set commit window failed: %s", esp_err_to_name(err));
            return err;
        }
        partial_io_area = io_area;
        sync_partial_previous = panel->model != SEEED_EPAPER_PANEL_SSD2677;
    }

    phase_start_ms = epaper_log_ms();
    err = panel->driver->refresh(panel, effective_mode);
    if (err != ESP_OK) {
        mark_panel_needs_reinit(panel);
        APP_EPAPER_DEBUG_LOGE(TAG, "refresh failed: %s", esp_err_to_name(err));
        return err;
    }
    APP_EPAPER_DEBUG_LOGI(TAG,
             "[perf] commit driver-refresh=%llums",
             (unsigned long long)(epaper_log_ms() - phase_start_ms));

    phase_start_ms = epaper_log_ms();
    if (sync_partial_previous) {
        const size_t row_bytes = packed_mono_stride(partial_io_area.width);
        const size_t sync_len = row_bytes * partial_io_area.height;
        uint8_t *current = (uint8_t *)alloc_temp_buffer(sync_len);
        if (current == NULL) {
            mark_panel_needs_reinit(panel);
            APP_EPAPER_DEBUG_LOGE(TAG, "alloc partial sync buffer failed");
            return ESP_ERR_NO_MEM;
        }

        err = copy_framebuffer_region(panel, &partial_io_area, current, sync_len);
        if (err == ESP_OK) {
            err = panel->driver->set_window(panel, &partial_io_area);
        }
        if (err == ESP_OK) {
            err = panel->driver->write_image(panel, current, sync_len, true);
        }
        if (err == ESP_OK) {
            err = panel->driver->write_image(panel, current, sync_len, false);
        }
        heap_caps_free(current);
        if (err != ESP_OK) {
            mark_panel_needs_reinit(panel);
            APP_EPAPER_DEBUG_LOGE(TAG, "sync partial planes failed: %s", esp_err_to_name(err));
            return err;
        }
        APP_EPAPER_DEBUG_LOGI(TAG,
                 "partial planes synced: len=%u",
                 (unsigned)sync_len);
    }
    if (effective_mode == SEEED_EPAPER_REFRESH_FULL) {
        panel->partial_ready = true;
        if (panel->previous_framebuffer != NULL &&
            panel->framebuffer != NULL &&
            panel->previous_framebuffer_len == panel->framebuffer_len) {
            memcpy(panel->previous_framebuffer, panel->framebuffer, panel->framebuffer_len);
        }
    } else if (effective_mode == SEEED_EPAPER_REFRESH_PARTIAL) {
        if (panel->previous_framebuffer != NULL &&
            panel->framebuffer != NULL &&
            panel->previous_framebuffer_len == panel->framebuffer_len) {
            memcpy(panel->previous_framebuffer, panel->framebuffer, panel->framebuffer_len);
        }
    } else if (effective_mode == SEEED_EPAPER_REFRESH_GRAY4 &&
               panel->model == SEEED_EPAPER_PANEL_SSD2677) {
        panel->partial_ready = true;
        if (panel->previous_framebuffer != NULL &&
            panel->framebuffer != NULL &&
            panel->previous_framebuffer_len == panel->framebuffer_len) {
            memcpy(panel->previous_framebuffer, panel->framebuffer, panel->framebuffer_len);
        }
    }
    panel->dirty_valid = false;
    panel->last_refresh_timing.state_sync_ms = (uint32_t)(epaper_log_ms() - phase_start_ms);
    APP_EPAPER_DEBUG_LOGI(TAG,
             "[perf] commit state-sync=%llums total=%llums",
             (unsigned long long)(epaper_log_ms() - phase_start_ms),
             (unsigned long long)(epaper_log_ms() - start_ms));
    APP_EPAPER_DEBUG_LOGI(TAG,
             "commit done: effective_mode=%s elapsed=%llums partial_ready=%d",
             refresh_mode_name(effective_mode),
             (unsigned long long)(epaper_log_ms() - start_ms),
             panel->partial_ready ? 1 : 0);
    return ESP_OK;
}

esp_err_t seeed_epaper_panel_refresh_area(seeed_epaper_panel_handle_t panel,
                                          const seeed_epaper_area_t *area,
                                          const void *pixels,
                                          size_t src_stride_bytes,
                                          seeed_epaper_pixel_format_t input_format,
                                          seeed_epaper_refresh_mode_t mode)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");
    const uint64_t start_ms = epaper_log_ms();
    APP_EPAPER_DEBUG_LOGI(TAG,
             "refresh_area start: mode=%s format=%s src_stride=%u",
             refresh_mode_name(mode),
             pixel_format_name(input_format),
             (unsigned)src_stride_bytes);
    log_area("refresh_area requested", area);
    seeed_epaper_area_t full_area = {
        .x = 0,
        .y = 0,
        .width = panel->info.width,
        .height = panel->info.height,
    };
    const seeed_epaper_area_t *effective_area = area != NULL ? area : &full_area;
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_write_bitmap(panel,
                                                        effective_area,
                                                        pixels,
                                                        src_stride_bytes,
                                                        input_format,
                                                        mode),
                        TAG,
                        "write bitmap failed");
    esp_err_t err = seeed_epaper_panel_commit(panel, effective_area, mode);
    APP_EPAPER_DEBUG_LOGI(TAG,
             "refresh_area done: mode=%s elapsed=%llums err=%s",
             refresh_mode_name(mode),
             (unsigned long long)(epaper_log_ms() - start_ms),
             esp_err_to_name(err));
    return err;
}

esp_err_t seeed_epaper_panel_clear(seeed_epaper_panel_handle_t panel,
                                   bool white,
                                   seeed_epaper_refresh_mode_t mode)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");

    const uint64_t start_ms = epaper_log_ms();
    const size_t frame_len = packed_mono_stride(panel->info.width) * panel->info.height;
    APP_EPAPER_DEBUG_LOGI(TAG,
             "clear start: white=%d mode=%s frame_len=%u",
             white ? 1 : 0,
             refresh_mode_name(mode),
             (unsigned)frame_len);
    uint8_t *buffer = (uint8_t *)alloc_temp_buffer(frame_len);
    ESP_RETURN_ON_FALSE(buffer != NULL, ESP_ERR_NO_MEM, TAG, "alloc clear buffer failed");
    memset(buffer, white ? 0xFF : 0x00, frame_len);

    esp_err_t err = seeed_epaper_panel_refresh_area(panel,
                                                    NULL,
                                                    buffer,
                                                    packed_mono_stride(panel->info.width),
                                                    SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB,
                                                    mode);
    heap_caps_free(buffer);
    APP_EPAPER_DEBUG_LOGI(TAG,
             "clear done: white=%d mode=%s elapsed=%llums err=%s",
             white ? 1 : 0,
             refresh_mode_name(mode),
             (unsigned long long)(epaper_log_ms() - start_ms),
             esp_err_to_name(err));
    return err;
}

esp_err_t seeed_epaper_panel_sleep(seeed_epaper_panel_handle_t panel)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");
    const uint64_t start_ms = epaper_log_ms();
    APP_EPAPER_DEBUG_LOGI(TAG,
             "sleep start: sleeping=%d standby=%d prepared=%d partial_ready=%d",
             panel->sleeping ? 1 : 0,
             panel->standby ? 1 : 0,
             panel->prepared ? 1 : 0,
             panel->partial_ready ? 1 : 0);
    if (panel->sleeping) {
        APP_EPAPER_DEBUG_LOGI(TAG,
                 "sleep skip: already sleeping elapsed=%llums",
                 (unsigned long long)(epaper_log_ms() - start_ms));
        return ESP_OK;
    }
    esp_err_t err = panel->driver->sleep(panel);
    if (err != ESP_OK) {
        mark_panel_needs_reinit(panel);
        APP_EPAPER_DEBUG_LOGE(TAG, "sleep failed: %s", esp_err_to_name(err));
        return err;
    }
    panel->sleeping = true;
    panel->prepared = false;
    panel->standby = false;
    panel->partial_after_standby = false;
    panel->partial_ready = false;
    panel->refresh_powered = false;
    panel->dirty_valid = false;
    panel->gray4_valid = false;
    APP_EPAPER_DEBUG_LOGI(TAG,
             "sleep done: elapsed=%llums",
             (unsigned long long)(epaper_log_ms() - start_ms));
    return ESP_OK;
}

esp_err_t seeed_epaper_panel_standby(seeed_epaper_panel_handle_t panel)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");
    const uint64_t start_ms = epaper_log_ms();
    APP_EPAPER_DEBUG_LOGI(TAG,
             "standby start: sleeping=%d standby=%d prepared=%d partial_ready=%d",
             panel->sleeping ? 1 : 0,
             panel->standby ? 1 : 0,
             panel->prepared ? 1 : 0,
             panel->partial_ready ? 1 : 0);
    ESP_RETURN_ON_FALSE(!panel->sleeping, ESP_ERR_INVALID_STATE, TAG, "panel is in deep sleep");
    if (panel->standby) {
        APP_EPAPER_DEBUG_LOGI(TAG,
                 "standby skip: already standby elapsed=%llums",
                 (unsigned long long)(epaper_log_ms() - start_ms));
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(panel->driver->standby != NULL, ESP_ERR_NOT_SUPPORTED, TAG, "driver standby not supported");
    esp_err_t err = panel->driver->standby(panel);
    if (err != ESP_OK) {
        mark_panel_needs_reinit(panel);
        APP_EPAPER_DEBUG_LOGE(TAG, "standby failed: %s", esp_err_to_name(err));
        return err;
    }
    panel->standby = true;
    panel->partial_after_standby = true;
    APP_EPAPER_DEBUG_LOGI(TAG,
             "standby done: elapsed=%llums",
             (unsigned long long)(epaper_log_ms() - start_ms));
    return ESP_OK;
}

esp_err_t seeed_epaper_panel_resume(seeed_epaper_panel_handle_t panel)
{
    ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "panel is null");
    const uint64_t start_ms = epaper_log_ms();
    panel->partial_after_standby = false;
    APP_EPAPER_DEBUG_LOGI(TAG,
             "resume start: sleeping=%d standby=%d prepared=%d partial_ready=%d",
             panel->sleeping ? 1 : 0,
             panel->standby ? 1 : 0,
             panel->prepared ? 1 : 0,
             panel->partial_ready ? 1 : 0);
    ESP_RETURN_ON_FALSE(!panel->sleeping, ESP_ERR_INVALID_STATE, TAG, "panel is in deep sleep");
    if (!panel->standby) {
        APP_EPAPER_DEBUG_LOGI(TAG,
                 "resume skip: panel not in standby elapsed=%llums",
                 (unsigned long long)(epaper_log_ms() - start_ms));
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(panel->driver->resume != NULL, ESP_ERR_NOT_SUPPORTED, TAG, "driver resume not supported");
    esp_err_t err = panel->driver->resume(panel);
    if (err != ESP_OK) {
        mark_panel_needs_reinit(panel);
        APP_EPAPER_DEBUG_LOGE(TAG, "resume failed: %s", esp_err_to_name(err));
        return err;
    }
    panel->standby = false;
    panel->prepared = true;
    APP_EPAPER_DEBUG_LOGI(TAG,
             "resume done: elapsed=%llums",
             (unsigned long long)(epaper_log_ms() - start_ms));
    return ESP_OK;
}
