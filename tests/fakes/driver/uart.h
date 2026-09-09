#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include <stddef.h>
#include <stdint.h>
esp_err_t uart_driver_install(int port, int rx, int tx, int queue_size, void *queue, int flags);
int uart_read_bytes(int port, void *data, uint32_t length, TickType_t wait);
int uart_write_bytes(int port, const void *data, size_t length);
esp_err_t uart_wait_tx_done(int port, TickType_t wait);
