#pragma once
#include "esp_err.h"

/* Initialize NVS before settings or workers. Security failures stop startup. */
esp_err_t storage_init(void);
