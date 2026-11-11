#pragma once
 typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_INVALID_SIZE 3
#define ESP_ERR_NO_MEM 4
#define ESP_ERR_NOT_SUPPORTED 5
#define ESP_ERR_TIMEOUT 6
const char *esp_err_to_name(esp_err_t err);
