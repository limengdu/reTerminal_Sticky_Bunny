/* Actual driver integration tests. Hardware is simulated, not the panel logic. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "epaper_panel_priv.h"
#include "esp_heap_caps.h"
#include "freertos/task.h"

enum { DC = 1, CS = 2, RST = 3, BUSY = 4, ENABLE = 5 };
static int levels[64];
static uint8_t chip_id, command;
static bool read_failure, command_failure, timeout_busy;
static size_t allocations, semaphores, handlers;
static size_t fail_allocation, allocation_attempt;
static uint64_t clock_ms;
static TickType_t last_wait;
static int last_edge;
static void (*busy_handler)(void *);
static void *busy_arg;
static uint8_t streams[256][200000];
static size_t lengths[256], commands[256];
static unsigned delays[128], delay_count;

void epaper_test_log(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }
const char *esp_err_to_name(esp_err_t err) { (void)err; return "mock error"; }
int64_t esp_timer_get_time(void) { return (int64_t)clock_ms * 1000; }
void vTaskDelay(TickType_t ticks) {
    clock_ms += ticks;
    if (delay_count < 128) delays[delay_count++] = ticks;
}
void *heap_caps_malloc(size_t n, unsigned caps) {
    (void)caps;
    if (++allocation_attempt == fail_allocation) return NULL;
    void *p = malloc(n); if (p) ++allocations; return p;
}
void *heap_caps_malloc_prefer(size_t n, size_t count, ...) {
    (void)count; return heap_caps_malloc(n, 0);
}
void heap_caps_free(void *p) { if (p) { assert(allocations); --allocations; free(p); } }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { ++semaphores; return calloc(1, sizeof(int)); }
void vSemaphoreDelete(SemaphoreHandle_t sem) { --semaphores; free(sem); }
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t sem, BaseType_t *wake) {
    *(int *)sem = 1; *wake = pdFALSE; return pdTRUE;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks) {
    if (*(int *)sem) { *(int *)sem = 0; return pdTRUE; }
    if (!ticks) return pdFALSE;
    assert(seeed_epaper_panel_busy_wait_active());
    last_wait = ticks;
    if (timeout_busy) { clock_ms += ticks; return pdFALSE; }
    clock_ms += 7;
    levels[BUSY] = !levels[BUSY];
    assert(busy_handler); busy_handler(busy_arg);
    assert(*(int *)sem); *(int *)sem = 0;
    return pdTRUE;
}
esp_err_t gpio_config(const gpio_config_t *config) { (void)config; return ESP_OK; }
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level) { levels[pin] = (int)level; return ESP_OK; }
int gpio_get_level(gpio_num_t pin) { return levels[pin]; }
esp_err_t gpio_set_intr_type(gpio_num_t pin, gpio_int_type_t edge) {
    assert(pin == BUSY); if (edge) last_edge = edge; return ESP_OK;
}
esp_err_t gpio_install_isr_service(int flags) { (void)flags; return ESP_OK; }
esp_err_t gpio_isr_handler_add(gpio_num_t pin, void (*fn)(void *), void *arg) {
    assert(pin == BUSY); busy_handler = fn; busy_arg = arg; ++handlers; return ESP_OK;
}
esp_err_t gpio_isr_handler_remove(gpio_num_t pin) {
    assert(pin == BUSY); --handlers; busy_handler = NULL; busy_arg = NULL; return ESP_OK;
}
esp_err_t spi_device_transmit(spi_device_handle_t spi, spi_transaction_t *trans) {
    assert(spi); assert(levels[CS] == 0);
    if (trans->flags & SPI_TRANS_USE_RXDATA) {
        assert(levels[DC] == 1); assert(trans->rxlength == 8);
        if (read_failure) return ESP_FAIL;
        trans->rx_data[0] = command == 0x70 ? chip_id : 25;
        return ESP_OK;
    }
    const size_t n = trans->length / 8;
    assert(n && n <= 1024 && trans->length % 8 == 0);
    if (!levels[DC]) {
        assert(n == 1); command = *(const uint8_t *)trans->tx_buffer;
        if (command_failure) return ESP_FAIL;
        ++commands[command];
    } else {
        assert(lengths[command] + n <= sizeof streams[command]);
        memcpy(streams[command] + lengths[command], trans->tx_buffer, n);
        lengths[command] += n;
    }
    return ESP_OK;
}
static void clear_trace(void) {
    memset(lengths, 0, sizeof lengths); memset(commands, 0, sizeof commands);
    delay_count = 0; last_wait = 0; last_edge = 0;
}
static seeed_epaper_panel_config_t config(void) {
    return (seeed_epaper_panel_config_t){ .spi_handle = (void *)1, .pin_dc = DC,
        .pin_cs = CS, .pin_rst = RST, .pin_busy = BUSY, .pin_enable = ENABLE,
        .manual_cs = true };
}
static seeed_epaper_panel_handle_t create(uint8_t id, seeed_epaper_panel_model_t requested) {
    chip_id = id; levels[BUSY] = 1; clear_trace();
    seeed_epaper_panel_config_t c = config();
    seeed_epaper_panel_handle_t p = NULL;
    assert(seeed_epaper_new_panel(requested, &c, &p) == ESP_OK);
    assert(p); assert(commands[0x70] == 1);
    assert(delay_count >= 2 && delays[0] == 20 && delays[1] == 10);
    levels[BUSY] = !p->config.busy_level;
    return p;
}
static void destroy(seeed_epaper_panel_handle_t p) {
    assert(seeed_epaper_panel_del(p) == ESP_OK);
    assert(!allocations && !semaphores && !handlers);
}
static void test_detection(void) {
    const uint8_t ids[] = {7, 0, 0xff, 6};
    for (unsigned requested = 1; requested <= 2; ++requested) {
        for (size_t i = 0; i < sizeof ids; ++i) {
            seeed_epaper_panel_handle_t p = create(ids[i], requested);
            const bool newer = ids[i] == 7;
            seeed_epaper_panel_model_t model;
            assert(seeed_epaper_panel_get_model(p, &model) == ESP_OK);
            assert(model == (newer ? SEEED_EPAPER_PANEL_SSD2677 : SEEED_EPAPER_PANEL_SSD1677));
            assert(p->config.busy_timeout_ms == (newer ? 30000U : 8000U));
            assert(p->config.reset_low_ms == (newer ? 20U : 10U));
            assert(p->config.reset_high_ms == 10);
            assert(p->config.busy_level == (newer ? 0 : 1));
            seeed_epaper_panel_info_t info;
            assert(seeed_epaper_panel_get_info(p, &info) == ESP_OK);
            assert(info.width == 800 && info.height == 480 && info.supports_gray4 && info.supports_partial);
            clear_trace();
            assert(seeed_epaper_panel_wait_ready(p) == ESP_OK && last_wait == 0);
            levels[BUSY] = p->config.busy_level;
            assert(seeed_epaper_panel_wait_ready(p) == ESP_OK);
            assert(last_wait == p->config.busy_timeout_ms);
            assert(last_edge == (newer ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE));
            assert(!seeed_epaper_panel_busy_wait_active());
            timeout_busy = true; levels[BUSY] = p->config.busy_level;
            assert(seeed_epaper_panel_wait_ready(p) == ESP_ERR_TIMEOUT);
            assert(!seeed_epaper_panel_busy_wait_active()); timeout_busy = false;
            destroy(p);
        }
    }
    for (unsigned failure = 0; failure < 2; ++failure) {
        seeed_epaper_panel_handle_t p = NULL;
        seeed_epaper_panel_config_t c = config(); levels[BUSY] = 1;
        read_failure = failure == 0; command_failure = failure == 1;
        assert(seeed_epaper_new_panel(SEEED_EPAPER_PANEL_SSD1677, &c, &p) == ESP_FAIL);
        assert(p == NULL && !allocations && !semaphores && !handlers);
        read_failure = false; command_failure = false;
    }
}
static void test_allocation_failures(void) {
    /* Sweep each heap-cap allocation in creation, including SPI probe storage. */
    bool reached_success = false;
    for (size_t fail = 1; fail < 10; ++fail) {
        seeed_epaper_panel_handle_t p = (void *)1;
        seeed_epaper_panel_config_t c = config();
        chip_id = 7; levels[BUSY] = 1; clear_trace();
        fail_allocation = fail; allocation_attempt = 0;
        esp_err_t err = seeed_epaper_new_panel(SEEED_EPAPER_PANEL_SSD1677, &c, &p);
        fail_allocation = 0;
        if (err == ESP_OK) { destroy(p); reached_success = true; break; }
        assert(err == ESP_ERR_NO_MEM && p == NULL);
        assert(!allocations && !semaphores && !handlers);
    }
    assert(reached_success);
}
static uint8_t mono[48000], old_mono[48000], gray[96000];
static const seeed_epaper_area_t full = {0, 0, 800, 480};
static void check_interleaved(const uint8_t *high, const uint8_t *low) {
    assert(lengths[0x10] == 96000 && commands[0x10] == 1);
    for (size_t pixel = 0; pixel < 384000; ++pixel) {
        unsigned bit = 7 - pixel % 8;
        uint8_t expected = (uint8_t)((((high[pixel / 8] >> bit) & 1) << 1) |
                                   ((low[pixel / 8] >> bit) & 1));
        assert(((streams[0x10][pixel / 4] >> (6 - 2 * (pixel % 4))) & 3) == expected);
    }
}
static void test_ssd2677(void) {
    seeed_epaper_panel_handle_t p = create(7, SEEED_EPAPER_PANEL_SSD1677);
    for (size_t i = 0; i < sizeof mono; ++i) mono[i] = (uint8_t)(i * 37 + i / 100);
    clear_trace();
    assert(seeed_epaper_panel_refresh_area(p, &full, mono, 100, SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB, SEEED_EPAPER_REFRESH_FULL) == ESP_OK);
    check_interleaved(mono, mono);
    assert(commands[0x04] == 1 && commands[0x12] == 1 && commands[0x02] == 1);
    assert(p->partial_ready && !p->refresh_powered);
    assert(memcmp(p->previous_framebuffer, mono, sizeof mono) == 0);
    assert(seeed_epaper_panel_standby(p) == ESP_OK && p->standby);
    memcpy(old_mono, mono, sizeof mono);
    for (size_t i = 0; i < sizeof mono; ++i) mono[i] ^= 0xa5;
    clear_trace();
    assert(seeed_epaper_panel_write_bitmap_diff(p, &full, old_mono, mono, 100, SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB, SEEED_EPAPER_REFRESH_PARTIAL) == ESP_OK);
    assert(seeed_epaper_panel_commit(p, &full, SEEED_EPAPER_REFRESH_PARTIAL) == ESP_OK);
    check_interleaved(old_mono, mono);
    assert(commands[0x04] == 1 && commands[0x02] == 0 && p->refresh_powered);
    assert(memcmp(p->previous_framebuffer, mono, sizeof mono) == 0);
    clear_trace();
    assert(seeed_epaper_panel_standby(p) == ESP_OK);
    assert(commands[0x02] == 1 && !p->refresh_powered && p->standby);
    /* prepare must resume automatically on the next gray refresh. */
    for (size_t i = 0; i < sizeof gray; ++i) gray[i] = (uint8_t)i;
    clear_trace();
    assert(seeed_epaper_panel_write_bitmap_gray4(p, &full, gray, 200) == ESP_OK);
    assert(seeed_epaper_panel_commit(p, &full, SEEED_EPAPER_REFRESH_GRAY4) == ESP_OK);
    assert(lengths[0x10] == sizeof gray && memcmp(streams[0x10], gray, sizeof gray) == 0);
    assert(p->partial_ready && !p->refresh_powered && !p->standby);
    seeed_epaper_refresh_timing_t timing;
    assert(seeed_epaper_panel_get_last_refresh_timing(p, &timing) == ESP_OK);
    assert(timing.temperature_valid && timing.temperature_c == 25 && timing.latch_apply_ms == 10);
    assert(memcmp(p->previous_framebuffer, p->framebuffer, sizeof mono) == 0);
    memcpy(old_mono, p->previous_framebuffer, sizeof old_mono);
    assert(seeed_epaper_panel_standby(p) == ESP_OK);
    clear_trace();
    assert(seeed_epaper_panel_refresh_area(p, &full, mono, 100, SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB, SEEED_EPAPER_REFRESH_PARTIAL) == ESP_OK);
    check_interleaved(old_mono, mono);
    assert(memcmp(p->previous_framebuffer, mono, sizeof mono) == 0);
    assert(p->refresh_powered);
    clear_trace();
    assert(seeed_epaper_panel_sleep(p) == ESP_OK && p->sleeping && !p->partial_ready);
    assert(commands[0x07] == 1 && streams[0x07][0] == 0xa5);
    assert(seeed_epaper_panel_resume(p) == ESP_ERR_INVALID_STATE);
    assert(seeed_epaper_panel_wakeup(p, SEEED_EPAPER_REFRESH_FULL) == ESP_OK && !p->sleeping);
    destroy(p);
}
static void test_mirrored_gray_shadow(void) {
    seeed_epaper_panel_handle_t p = create(7, SEEED_EPAPER_PANEL_SSD1677);
    p->config.mirror_x = true;
    memset(gray, 0xff, sizeof gray);
    gray[0] = 0x3f; /* One black pixel at logical (0, 0). */
    clear_trace();
    assert(seeed_epaper_panel_write_bitmap_gray4(p, &full, gray, 200) == ESP_OK);
    assert(seeed_epaper_panel_commit(p, &full, SEEED_EPAPER_REFRESH_GRAY4) == ESP_OK);
    assert(streams[0x10][0] == 0xff && streams[0x10][199] == 0xfc);
    memset(old_mono, 0xff, sizeof old_mono); old_mono[99] = 0xfe;
    assert(memcmp(p->previous_framebuffer, old_mono, sizeof old_mono) == 0);
    assert(seeed_epaper_panel_standby(p) == ESP_OK);
    memset(mono, 0xff, sizeof mono); mono[0] = 0xbf; /* logical pixel 1 black */
    clear_trace();
    assert(seeed_epaper_panel_refresh_area(p, &full, mono, 100, SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB, SEEED_EPAPER_REFRESH_PARTIAL) == ESP_OK);
    memset(mono, 0xff, sizeof mono); mono[99] = 0xfd;
    check_interleaved(old_mono, mono);
    destroy(p);
}
static void test_ssd1677(void) {
    seeed_epaper_panel_handle_t p = create(0, SEEED_EPAPER_PANEL_SSD2677);
    memset(mono, 0xa5, sizeof mono); clear_trace();
    assert(seeed_epaper_panel_refresh_area(p, &full, mono, 100, SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB, SEEED_EPAPER_REFRESH_FULL) == ESP_OK);
    assert(lengths[0x24] == 48000 && lengths[0x26] == 48000);
    assert(memcmp(streams[0x24], mono, sizeof mono) == 0);
    const seeed_epaper_area_t small = {8, 3, 8, 1}; uint8_t pixel = 0x3c;
    clear_trace();
    assert(seeed_epaper_panel_refresh_area(p, &small, &pixel, 1, SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB, SEEED_EPAPER_REFRESH_PARTIAL) == ESP_OK);
    assert(streams[0x24][lengths[0x24] - 1] == pixel);
    assert(streams[0x26][lengths[0x26] - 1] == pixel);
    assert(p->framebuffer[301] == pixel && p->previous_framebuffer[301] == pixel);
    assert(seeed_epaper_panel_sleep(p) == ESP_OK);
    clear_trace(); memset(gray, 0x1b, sizeof gray);
    assert(seeed_epaper_panel_write_bitmap_gray4(p, &full, gray, 200) == ESP_OK);
    assert(lengths[0x24] == 48000 && lengths[0x26] == 48000);
    assert(delay_count >= 2 && delays[0] == 10 && delays[1] == 10);
    for (size_t i = 0; i < 48000; ++i) {
        assert(streams[0x24][i] == 0xcc && streams[0x26][i] == 0xaa);
    }
    assert(seeed_epaper_panel_commit(p, &full, SEEED_EPAPER_REFRESH_GRAY4) == ESP_OK);
    destroy(p);
}
static void test_ssd1677_mirrored_region(void) {
    seeed_epaper_panel_handle_t p = create(0, SEEED_EPAPER_PANEL_SSD1677);
    p->config.mirror_x = true;
    memset(mono, 0xff, sizeof mono);
    mono[1] = 0x96; mono[98] = 0x3c;
    assert(seeed_epaper_panel_refresh_area(p, &full, mono, 100,
        SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB, SEEED_EPAPER_REFRESH_FULL) == ESP_OK);
    /* Software mirror: logical x8 byte 0x96 becomes physical x784 byte 0x69. */
    assert(p->framebuffer[98] == 0x69 && p->framebuffer[1] == 0x3c);
    assert(seeed_epaper_panel_standby(p) == ESP_OK);
    const seeed_epaper_area_t small = {8, 0, 8, 1};
    const uint8_t pixel = 0x12;
    clear_trace();
    assert(seeed_epaper_panel_write_bitmap(p, &small, &pixel, 1,
        SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB, SEEED_EPAPER_REFRESH_PARTIAL) == ESP_OK);
    assert(lengths[0x26] == 1 && streams[0x26][0] == 0x69);
    assert(lengths[0x24] == 1 && streams[0x24][0] == 0x48);
    assert(seeed_epaper_panel_commit(p, &small, SEEED_EPAPER_REFRESH_PARTIAL) == ESP_OK);
    assert(p->framebuffer[98] == 0x48 && p->previous_framebuffer[98] == 0x48);
    assert(p->framebuffer[1] == 0x3c);
    destroy(p);
}

/* Repeat the same standby/partial path used when paging through the tutorial. */
static void test_ssd1677_repeated_standby_partial(void) {
    seeed_epaper_panel_handle_t p = create(0xff, SEEED_EPAPER_PANEL_SSD1677);
    memset(mono, 0xff, sizeof mono);
    assert(seeed_epaper_panel_refresh_area(p, &full, mono, 100,
        SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB, SEEED_EPAPER_REFRESH_FULL) == ESP_OK);
    assert(seeed_epaper_panel_standby(p) == ESP_OK);
    const seeed_epaper_area_t small = {8, 3, 8, 1};
    for (unsigned cycle = 0; cycle < 6; ++cycle) {
        const uint8_t pixel = (uint8_t)(0x3c ^ cycle);
        clear_trace();
        assert(seeed_epaper_panel_write_bitmap(p, &small, &pixel, 1,
            SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB, SEEED_EPAPER_REFRESH_PARTIAL) == ESP_OK);
        assert(!p->standby && p->prepared && !p->partial_after_standby);
        assert(seeed_epaper_panel_commit(p, &small, SEEED_EPAPER_REFRESH_PARTIAL) == ESP_OK);
        assert(seeed_epaper_panel_standby(p) == ESP_OK && p->standby);
        const uint8_t *updates = streams[0x22];
        if (cycle == 0) {
            assert(lengths[0x22] == 2 && updates[0] == 0xfc && updates[1] == 0x03);
        } else {
            assert(lengths[0x22] == 3);
            assert(updates[0] == 0xc0 && updates[1] == 0xfc && updates[2] == 0x03);
        }
        assert(p->previous_framebuffer[301] == pixel);
    }
    clear_trace();
    assert(seeed_epaper_panel_resume(p) == ESP_OK && !p->partial_after_standby);
    assert(lengths[0x22] == 1 && streams[0x22][0] == 0xc0);
    destroy(p);
}

static void test_ssd1677_gray_partial_fallback(void) {
    seeed_epaper_panel_handle_t p = create(0, SEEED_EPAPER_PANEL_SSD1677);
    memset(mono, 0xa5, sizeof mono);
    assert(seeed_epaper_panel_refresh_area(p, &full, mono, 100,
        SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB, SEEED_EPAPER_REFRESH_FULL) == ESP_OK);
    assert(p->partial_ready);
    assert(seeed_epaper_panel_standby(p) == ESP_OK);
    memset(gray, 0x1b, sizeof gray);
    assert(seeed_epaper_panel_write_bitmap_gray4(p, &full, gray, 200) == ESP_OK);
    assert(seeed_epaper_panel_commit(p, &full, SEEED_EPAPER_REFRESH_GRAY4) == ESP_OK);
    assert(!p->partial_ready);
    assert(seeed_epaper_panel_standby(p) == ESP_OK);
    clear_trace(); memset(mono, 0x3c, sizeof mono);
    assert(seeed_epaper_panel_refresh_area(p, &full, mono, 100,
        SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB, SEEED_EPAPER_REFRESH_PARTIAL) == ESP_OK);
    assert(p->active_mode == SEEED_EPAPER_REFRESH_FULL && p->partial_ready);
    assert(lengths[0x24] == sizeof mono && lengths[0x26] == sizeof mono);
    assert(memcmp(streams[0x24], mono, sizeof mono) == 0);
    assert(memcmp(streams[0x26], mono, sizeof mono) == 0);
    assert(memcmp(p->previous_framebuffer, mono, sizeof mono) == 0);
    destroy(p);
}

int main(void) {
    test_detection(); test_allocation_failures(); test_ssd2677(); test_ssd1677(); test_mirrored_gray_shadow();
    test_ssd1677_mirrored_region(); test_ssd1677_gray_partial_fallback();
    test_ssd1677_repeated_standby_partial();
    puts("ePaper actual-driver integration tests passed");
    return 0;
}
