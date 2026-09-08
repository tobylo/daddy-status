#pragma once
#include "esp_err.h"
#define ESP_ERR_NVS_NO_FREE_PAGES 0x110d
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x1110
#define ESP_ERR_NVS_CORRUPT_KEY_PART 0x1117
#define ESP_ERR_NVS_WRONG_ENCRYPTION 0x1119
esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);
#include "esp_partition.h"
#define ESP_ERR_NVS_KEYS_NOT_INITIALIZED 0x1116
typedef struct {
    unsigned char eky[32], tky[32];
} nvs_sec_cfg_t;
esp_err_t nvs_flash_read_security_cfg(const esp_partition_t *partition, nvs_sec_cfg_t *keys);
esp_err_t nvs_flash_generate_keys(const esp_partition_t *partition, nvs_sec_cfg_t *keys);
esp_err_t nvs_flash_secure_init(nvs_sec_cfg_t *keys);
