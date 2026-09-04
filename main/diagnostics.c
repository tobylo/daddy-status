#include "diagnostics.h"
#include "sdkconfig.h"
#if CONFIG_STATUS_DIAGNOSTICS
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

void diagnostics_sample(const char *task, int64_t *last_sample_us)
{
#if CONFIG_STATUS_DIAGNOSTICS
    int64_t now = esp_timer_get_time();
    if (now - *last_sample_us < 60000000)
        return;
    *last_sample_us = now;
    ESP_LOGI(task, "heap_free=%u heap_min=%u task_stack_min=%u bytes",
             (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size(),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
#else
    (void)task;
    (void)last_sample_us;
#endif
}
