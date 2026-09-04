#ifndef DADDY_TASK_TIME_H
#define DADDY_TASK_TIME_H
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Preserve a scheduling opportunity even when milliseconds round below one tick. */
static inline TickType_t task_ticks_ms(unsigned milliseconds)
{
    TickType_t ticks = pdMS_TO_TICKS(milliseconds);
    return ticks ? ticks : 1;
}

/* Chunk long Retry-After delays so conversion to RTOS ticks cannot overflow. */
static inline void task_wait_seconds(unsigned seconds)
{
    while (seconds) {
        unsigned part = seconds > 60 ? 60 : seconds;
        vTaskDelay(task_ticks_ms(part * 1000));
        seconds -= part;
    }
}
#endif
