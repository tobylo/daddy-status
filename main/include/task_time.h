#ifndef DADDY_TASK_TIME_H
#define DADDY_TASK_TIME_H
#include "esp_timer.h"
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
/* Keep request starts on a cadence; slow requests skip missed slots. */
static inline void task_wait_poll_slot(int64_t started_us, unsigned interval_seconds)
{
    int64_t period = (int64_t)interval_seconds * 1000000;
    if (period <= 0)
        return;
    int64_t now = esp_timer_get_time();
    int64_t elapsed = now > started_us ? now - started_us : 0;
    int64_t next = started_us + (elapsed / period + 1) * period;
    while ((now = esp_timer_get_time()) < next) {
        int64_t milliseconds = (next - now + 999) / 1000;
        vTaskDelay(task_ticks_ms(milliseconds > 60000 ? 60000 : (unsigned)milliseconds));
    }
}
#endif
