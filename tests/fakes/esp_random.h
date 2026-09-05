#include <stdint.h>
uint32_t esp_random(void);

#include <stddef.h>
void esp_fill_random(void *buffer, size_t length);
