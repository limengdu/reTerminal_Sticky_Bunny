#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef int gpio_num_t;
typedef int gpio_int_type_t;
#define GPIO_INTR_DISABLE 0
#define GPIO_INTR_POSEDGE 1
#define GPIO_INTR_NEGEDGE 2
#define GPIO_MODE_OUTPUT 1
#define GPIO_MODE_INPUT 2
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_PULLUP_DISABLE 0
typedef struct { uint64_t pin_bit_mask; int mode; int pull_up_en; int pull_down_en; int intr_type; } gpio_config_t;
esp_err_t gpio_config(const gpio_config_t *config);
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level);
int gpio_get_level(gpio_num_t pin);
esp_err_t gpio_set_intr_type(gpio_num_t pin, gpio_int_type_t edge);
esp_err_t gpio_install_isr_service(int flags);
esp_err_t gpio_isr_handler_add(gpio_num_t pin, void (*fn)(void *), void *arg);
esp_err_t gpio_isr_handler_remove(gpio_num_t pin);
