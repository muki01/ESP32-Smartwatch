#pragma once
typedef int esp_err_t;
#define ESP_OK 0
static inline esp_err_t nvs_flash_erase() { return ESP_OK; }
static inline esp_err_t nvs_flash_init() { return ESP_OK; }
