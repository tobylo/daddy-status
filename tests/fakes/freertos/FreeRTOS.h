#ifndef TEST_FREERTOS_H
#define TEST_FREERTOS_H
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef void *TaskHandle_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY UINT32_MAX
#ifndef TEST_FREERTOS_HZ
#define TEST_FREERTOS_HZ 1000
#endif
#define pdMS_TO_TICKS(ms) ((TickType_t)(((uint64_t)(ms) * TEST_FREERTOS_HZ) / 1000))
#endif
