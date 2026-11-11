#pragma once
#include "esp_log.h"
#define ESP_RETURN_ON_FALSE(a, err, ...) do { if (!(a)) { epaper_test_log(__VA_ARGS__); return (err); } } while (0)
#define ESP_RETURN_ON_ERROR(a, ...) do { esp_err_t stub_err = (a); if (stub_err != ESP_OK) { epaper_test_log(__VA_ARGS__); return stub_err; } } while (0)
