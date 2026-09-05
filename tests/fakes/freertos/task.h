#include "FreeRTOS.h"
void vTaskDelay(TickType_t ticks);
BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *arg,
                       unsigned priority, TaskHandle_t *handle);
void vTaskSuspend(TaskHandle_t task);
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait);
void xTaskNotifyGive(TaskHandle_t task);
