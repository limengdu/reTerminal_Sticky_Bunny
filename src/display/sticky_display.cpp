#include "sticky_display.h"
#include "sticky_display_spi_config.h"

#include <atomic>
#include <cstring>
#include <new>

#include "app_log.h"
#include "battery_status_overlay.h"
#include "canvas.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "epaper_panel.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pin_config.h"

namespace {

constexpr char kTag[] = "sticky_display";
constexpr size_t kFramebufferStride = kStickyDisplayWidth / 4U;
constexpr size_t kFramebufferSize = kFramebufferStride * kStickyDisplayHeight;
constexpr size_t kMonochromeStride = kStickyDisplayWidth / 8U;
constexpr uint8_t kFastAppTransitionsBeforeCleanup = 5U;

seeed_epaper_panel_handle_t s_panel = nullptr;
spi_device_handle_t s_spi_device = nullptr;
uint8_t *s_framebuffer = nullptr;
uint8_t *s_rotated_framebuffer = nullptr;
Canvas *s_canvas = nullptr;
std::atomic<bool> s_fast_app_refresh_armed{false};
std::atomic<bool> s_battery_overlay_enabled{true};
std::atomic<bool> s_battery_overlay_sleep_layout{false};
uint8_t s_fast_app_transition_count = 0U;

esp_err_t finish_refresh(esp_err_t result)
{
    // Preserve the comparison frame, but disable the panel's high-voltage supply
    // while Bunny is idle. prepare() resumes automatically on the next update.
    // 保留对比帧，空闲时关闭屏幕高压；下一次刷新由 prepare() 自动恢复。
    if (result != ESP_OK) {
        return result;
    }
    return seeed_epaper_panel_standby(s_panel);
}

void draw_battery_overlay_if_enabled()
{
    if (s_battery_overlay_enabled.load(std::memory_order_acquire)) {
        battery_status_overlay_draw(
            *s_canvas,
            s_battery_overlay_sleep_layout.load(std::memory_order_acquire));
    }
}

uint8_t reverse_pixel_order(uint8_t packed_pixels)
{
    return static_cast<uint8_t>(((packed_pixels & 0x03U) << 6) |
                                ((packed_pixels & 0x0CU) << 2) |
                                ((packed_pixels & 0x30U) >> 2) |
                                ((packed_pixels & 0xC0U) >> 6));
}

void rotate_framebuffer_180(const uint8_t *source, uint8_t *destination)
{
    // Four 2-bit pixels are packed into each byte. Reversing both the byte
    // order and the four pixel groups produces a complete 180-degree rotation.
    // 每个字节保存四个2位像素；同时反转字节顺序和字节内像素顺序即可旋转180度。
    for (size_t source_index = 0; source_index < kFramebufferSize; ++source_index) {
        const size_t destination_index = kFramebufferSize - 1U - source_index;
        destination[destination_index] = reverse_pixel_order(source[source_index]);
    }
}

void convert_gray4_to_monochrome_in_place(uint8_t *buffer)
{
    // Canvas pixels use two bits: Black=0 and White=3. The conversion runs
    // forward safely in place because each 1bpp output row is half the size
    // of its 2bpp input row. Values 2 and 3 map to white; 0 and 1 map to black.
    // 输出行只有输入行的一半，因此可以从前向后原地转换；灰度2/3转白，0/1转黑。
    for (size_t y = 0; y < kStickyDisplayHeight; ++y) {
        const size_t source_row = y * kFramebufferStride;
        const size_t destination_row = y * kMonochromeStride;

        for (size_t byte_x = 0; byte_x < kMonochromeStride; ++byte_x) {
            uint8_t monochrome_pixels = 0;
            for (size_t bit = 0; bit < 8U; ++bit) {
                const size_t pixel_x = byte_x * 8U + bit;
                const uint8_t packed = buffer[source_row + pixel_x / 4U];
                const uint8_t shift =
                    static_cast<uint8_t>((3U - (pixel_x & 0x03U)) * 2U);
                const uint8_t gray = static_cast<uint8_t>((packed >> shift) & 0x03U);
                if (gray >= 2U) {
                    monochrome_pixels |= static_cast<uint8_t>(1U << (7U - bit));
                }
            }
            buffer[destination_row + byte_x] = monochrome_pixels;
        }
    }
}

}  // namespace

esp_err_t sticky_display_init()
{
    app_log_register_tag(kTag);
    app_log_register_tag("seeed_epaper");
    app_log_register_tag("ssd1677");
    app_log_register_tag("ssd2677");

    if (s_panel != nullptr && s_canvas != nullptr) {
        STICKY_LOGD(kTag, "display=init state=already_ready");
        return ESP_OK;
    }

    const int64_t init_started_us = esp_timer_get_time();
    STICKY_LOGI(kTag,
                "display=init_begin width=%u height=%u",
                static_cast<unsigned>(kStickyDisplayWidth),
                static_cast<unsigned>(kStickyDisplayHeight));

    gpio_config_t power_config = {};
    power_config.pin_bit_mask = 1ULL << PIN_EPD_EN;
    power_config.mode = GPIO_MODE_OUTPUT;
    power_config.pull_up_en = GPIO_PULLUP_DISABLE;
    power_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    power_config.intr_type = GPIO_INTR_DISABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&power_config), "sticky_display", "configure display power");
    ESP_RETURN_ON_ERROR(gpio_set_level(static_cast<gpio_num_t>(PIN_EPD_EN), 1),
                        "sticky_display", "enable display power");
    vTaskDelay(pdMS_TO_TICKS(100));
    STICKY_LOGD(kTag,
                "display=power_on pin=%d settle_ms=100 result=ok",
                PIN_EPD_EN);

    spi_bus_config_t bus_config = {};
    bus_config.mosi_io_num = PIN_EPD_MOSI;
    bus_config.miso_io_num = PIN_EPD_MISO;
    bus_config.sclk_io_num = PIN_EPD_CLK;
    // ESP-IDF requires every unused SPI data pin to be -1. Leaving these
    // zero-initialized assigns GPIO0 to the SPI bus, but GPIO0 is also the
    // sensor I2C SCL pin. That made SHT40 work before display init and fail
    // immediately afterwards.
    // ESP-IDF要求未使用的SPI数据脚显式设为-1，否则零初始化会错误占用GPIO0。
    bus_config.quadwp_io_num = -1;
    bus_config.quadhd_io_num = -1;
    bus_config.data4_io_num = -1;
    bus_config.data5_io_num = -1;
    bus_config.data6_io_num = -1;
    bus_config.data7_io_num = -1;
    bus_config.max_transfer_sz = kStickyDisplayWidth * kStickyDisplayHeight / 8;
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO),
                        "sticky_display", "initialize SPI2");
    STICKY_LOGD(kTag,
                "display=spi_bus_ready host=%d mosi=%d miso=%d clk=%d result=ok",
                static_cast<int>(SPI2_HOST),
                PIN_EPD_MOSI,
                PIN_EPD_MISO,
                PIN_EPD_CLK);

    const spi_device_interface_config_t device_config = sticky_display_spi_device_config();
    ESP_RETURN_ON_ERROR(spi_bus_add_device(SPI2_HOST, &device_config, &s_spi_device),
                        "sticky_display", "add display to SPI2");
    STICKY_LOGI(kTag,
                "display=spi_device_ready cs=%d clock_hz=%d data_io=%d mode=0 half_duplex=1 result=ok",
                PIN_EPD_CS,
                device_config.clock_speed_hz,
                PIN_EPD_MOSI);

    seeed_epaper_panel_config_t panel_config = {};
    panel_config.spi_handle = s_spi_device;
    panel_config.pin_cs = static_cast<gpio_num_t>(PIN_EPD_CS);
    panel_config.pin_dc = static_cast<gpio_num_t>(PIN_EPD_DC);
    panel_config.pin_rst = static_cast<gpio_num_t>(PIN_EPD_RST);
    panel_config.pin_busy = static_cast<gpio_num_t>(PIN_EPD_BUSY);
    panel_config.pin_enable = GPIO_NUM_NC;
    panel_config.busy_timeout_ms = 10000;
    panel_config.reset_low_ms = 10;
    panel_config.reset_high_ms = 10;
    panel_config.busy_level = 1;
    panel_config.busy_level_valid = true;
    panel_config.enable_level = 1;
    panel_config.enable_level_valid = true;
    panel_config.mirror_x = true;
    ESP_RETURN_ON_ERROR(
        seeed_epaper_new_panel(SEEED_EPAPER_PANEL_SSD1677, &panel_config, &s_panel),
        "sticky_display", "detect and create Sticky panel");
    seeed_epaper_panel_model_t detected_model;
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_get_model(s_panel, &detected_model),
                        "sticky_display", "read detected panel model");
    STICKY_LOGI(kTag,
                "display=panel_ready controller=%s dc=%d rst=%d busy=%d result=ok",
                detected_model == SEEED_EPAPER_PANEL_SSD2677 ? "SSD2677" : "SSD1677",
                PIN_EPD_DC,
                PIN_EPD_RST,
                PIN_EPD_BUSY);

    s_framebuffer = static_cast<uint8_t *>(
        heap_caps_malloc(kFramebufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    s_rotated_framebuffer = static_cast<uint8_t *>(
        heap_caps_malloc(kFramebufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (s_framebuffer == nullptr || s_rotated_framebuffer == nullptr) {
        heap_caps_free(s_framebuffer);
        heap_caps_free(s_rotated_framebuffer);
        s_framebuffer = nullptr;
        s_rotated_framebuffer = nullptr;
        return ESP_ERR_NO_MEM;
    }

    s_canvas = new (std::nothrow)
        Canvas(kStickyDisplayWidth, kStickyDisplayHeight, s_framebuffer, kFramebufferSize);
    if (s_canvas == nullptr) {
        heap_caps_free(s_framebuffer);
        heap_caps_free(s_rotated_framebuffer);
        s_framebuffer = nullptr;
        s_rotated_framebuffer = nullptr;
        return ESP_ERR_NO_MEM;
    }
    s_canvas->clear();
    STICKY_LOGI(kTag,
                "display=framebuffer_ready buffers=2 bytes_each=%u memory=psram result=ok",
                static_cast<unsigned>(kFramebufferSize));
    STICKY_LOGI(kTag,
                "display=init_done elapsed_ms=%lld result=ok",
                static_cast<long long>((esp_timer_get_time() - init_started_us) / 1000));
    return ESP_OK;
}

Canvas *sticky_display_canvas()
{
    return s_canvas;
}

esp_err_t sticky_display_refresh()
{
    if (s_panel == nullptr || s_canvas == nullptr || s_rotated_framebuffer == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    const int64_t refresh_started_us = esp_timer_get_time();
    STICKY_LOGI(kTag, "display=refresh_begin mode=gray4");
    draw_battery_overlay_if_enabled();
    rotate_framebuffer_180(s_canvas->data(), s_rotated_framebuffer);

    const seeed_epaper_area_t full_screen = {
        0, 0, kStickyDisplayWidth, kStickyDisplayHeight,
    };
    ESP_RETURN_ON_ERROR(
        seeed_epaper_panel_write_bitmap_gray4(
            s_panel, &full_screen, s_rotated_framebuffer, s_canvas->stride()),
        "sticky_display", "write gray4 framebuffer");
    const esp_err_t result =
        finish_refresh(seeed_epaper_panel_commit(
            s_panel, &full_screen, SEEED_EPAPER_REFRESH_GRAY4));
    STICKY_LOGI(kTag,
                "display=refresh_done mode=gray4 elapsed_ms=%lld result=%s",
                static_cast<long long>((esp_timer_get_time() - refresh_started_us) / 1000),
                esp_err_to_name(result));
    return result;
}

esp_err_t sticky_display_refresh_partial()
{
    if (s_panel == nullptr || s_canvas == nullptr ||
        s_rotated_framebuffer == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    // Both controllers need a complete comparison frame for partial updates;
    // SSD2677 packs previous/current pixels into one full-screen 2bpp stream.
    // 两种控制器局刷都需要完整对比帧；SSD2677 将前后帧打包为全屏 2bpp 数据。
    draw_battery_overlay_if_enabled();
    rotate_framebuffer_180(s_canvas->data(), s_rotated_framebuffer);
    convert_gray4_to_monochrome_in_place(s_rotated_framebuffer);

    const seeed_epaper_area_t full_screen = {
        0, 0, kStickyDisplayWidth, kStickyDisplayHeight,
    };
    return finish_refresh(seeed_epaper_panel_refresh_area(
        s_panel,
        &full_screen,
        s_rotated_framebuffer,
        kMonochromeStride,
        SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB,
        SEEED_EPAPER_REFRESH_PARTIAL));
}

esp_err_t sticky_display_refresh_monochrome()
{
    if (s_panel == nullptr || s_canvas == nullptr ||
        s_rotated_framebuffer == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    const bool fast_refresh =
        s_fast_app_refresh_armed.exchange(false, std::memory_order_acq_rel);
    const int64_t refresh_started_us = esp_timer_get_time();
    if (fast_refresh) {
        STICKY_LOGI(kTag, "display=refresh_begin mode=monochrome_fast");
        const esp_err_t result = sticky_display_refresh_partial();
        STICKY_LOGI(kTag,
                    "display=refresh_done mode=monochrome_fast elapsed_ms=%lld result=%s",
                    static_cast<long long>(
                        (esp_timer_get_time() - refresh_started_us) / 1000),
                    esp_err_to_name(result));
        return result;
    }
    STICKY_LOGI(kTag, "display=refresh_begin mode=monochrome_full");
    draw_battery_overlay_if_enabled();
    rotate_framebuffer_180(s_canvas->data(), s_rotated_framebuffer);
    convert_gray4_to_monochrome_in_place(s_rotated_framebuffer);

    const seeed_epaper_area_t full_screen = {
        0, 0, kStickyDisplayWidth, kStickyDisplayHeight,
    };
    const esp_err_t result = finish_refresh(seeed_epaper_panel_refresh_area(
        s_panel,
        &full_screen,
        s_rotated_framebuffer,
        kMonochromeStride,
        SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB,
        SEEED_EPAPER_REFRESH_FULL));
    STICKY_LOGI(kTag,
                "display=refresh_done mode=monochrome_full elapsed_ms=%lld result=%s",
                static_cast<long long>((esp_timer_get_time() - refresh_started_us) / 1000),
                esp_err_to_name(result));
    return result;
}

bool sticky_display_prepare_app_transition_refresh()
{
    if (s_fast_app_transition_count >=
        kFastAppTransitionsBeforeCleanup) {
        s_fast_app_transition_count = 0U;
        s_fast_app_refresh_armed.store(false, std::memory_order_release);
        STICKY_LOGI(kTag,
                    "display=transition_refresh mode=full reason=periodic_cleanup");
        return false;
    }

    ++s_fast_app_transition_count;
    s_fast_app_refresh_armed.store(true, std::memory_order_release);
    STICKY_LOGI(kTag,
                "display=transition_refresh mode=fast sequence=%u/%u",
                static_cast<unsigned>(s_fast_app_transition_count),
                static_cast<unsigned>(kFastAppTransitionsBeforeCleanup));
    return true;
}

void sticky_display_cancel_app_transition_refresh()
{
    s_fast_app_refresh_armed.store(false, std::memory_order_release);
}

void sticky_display_set_battery_overlay_enabled(bool enabled)
{
    s_battery_overlay_enabled.store(enabled, std::memory_order_release);
}

void sticky_display_set_battery_overlay_sleep_layout(bool sleep_layout)
{
    if (s_canvas != nullptr) {
        const bool current_layout =
            s_battery_overlay_sleep_layout.load(std::memory_order_acquire);
        battery_status_overlay_clear(*s_canvas, current_layout);
    }
    s_battery_overlay_sleep_layout.store(
        sleep_layout, std::memory_order_release);
}

esp_err_t sticky_display_clear()
{
    if (s_panel == nullptr || s_canvas == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    const int64_t clear_started_us = esp_timer_get_time();
    STICKY_LOGI(kTag, "display=clear_begin color=white mode=full");
    s_canvas->clear();
    const esp_err_t result =
        finish_refresh(seeed_epaper_panel_clear(s_panel, true, SEEED_EPAPER_REFRESH_FULL));
    STICKY_LOGI(kTag,
                "display=clear_done color=white mode=full elapsed_ms=%lld result=%s",
                static_cast<long long>(
                    (esp_timer_get_time() - clear_started_us) / 1000),
                esp_err_to_name(result));
    return result;
}

esp_err_t sticky_display_sleep()
{
    if (s_panel == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(
        seeed_epaper_panel_sleep(s_panel),
        "sticky_display", "put panel to sleep");
    return gpio_set_level(static_cast<gpio_num_t>(PIN_EPD_EN), 0);
}
