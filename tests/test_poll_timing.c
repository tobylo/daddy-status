#include "task_time.h"
#include <assert.h>
#include <stdio.h>
static int64_t now;
static unsigned calls;
int64_t esp_timer_get_time(void)
{
    return now;
}
void vTaskDelay(TickType_t ticks)
{
    assert(ticks > 0);
    ++calls;
    now += (int64_t)ticks * 1000000 / TEST_FREERTOS_HZ;
}
int main(void)
{
    now = 2000000;
    task_wait_poll_slot(0, 10);
    assert(now == 10000000);
    now = 12000000;
    task_wait_poll_slot(0, 10);
    assert(now == 20000000);
    now = 10000000;
    task_wait_poll_slot(0, 10);
    assert(now == 20000000);
    now = 35000000;
    task_wait_poll_slot(0, 10);
    assert(now == 40000000);
    now = 1;
    task_wait_poll_slot(0, 10);
    assert(now >= 10000000 && now < 10001000);
    now = 0;
    calls = 0;
    task_wait_poll_slot(0, 300);
    assert(now == 300000000 && calls == 5);
    puts("Poll cadence, overruns, missed slots, rounding, and bounded sleeps passed");
}
