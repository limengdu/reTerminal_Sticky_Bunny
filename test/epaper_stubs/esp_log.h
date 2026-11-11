#pragma once
void epaper_test_log(const char *tag, const char *fmt, ...);
#define ESP_LOGI(...) epaper_test_log(__VA_ARGS__)
#define ESP_LOGE(...) epaper_test_log(__VA_ARGS__)
#define ESP_LOGW(...) epaper_test_log(__VA_ARGS__)
#define ESP_LOGD(...) epaper_test_log(__VA_ARGS__)
