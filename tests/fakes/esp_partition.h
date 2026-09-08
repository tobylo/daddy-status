#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
typedef struct {
    size_t size;
    bool encrypted;
} esp_partition_t;
typedef int esp_partition_type_t;
typedef int esp_partition_subtype_t;
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_DATA_NVS 2
#define ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS 4
const esp_partition_t *esp_partition_find_first(esp_partition_type_t type,
                                                esp_partition_subtype_t subtype, const char *label);
esp_err_t esp_partition_read_raw(const esp_partition_t *partition, size_t offset, void *data,
                                 size_t length);
