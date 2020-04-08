#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "interrupt.h"
#include "timer.h"

#define TIMER_DIVIDER         16  //  Hardware timer clock divider
#define TIMER_SCALE           (TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds
#define TIMER_INTERVAL        (0.1)
#define TIMER_IDX             0

static xQueueHandle *timer_queue = NULL;

static void IRAM_ATTR timer_isr(void *para)
{
    /* Retrieve the interrupt status and the counter value
       from the timer that reported the interrupt */
    uint32_t intr_status = TIMERG0.int_st_timers.val;
    TIMERG0.hw_timer[TIMER_IDX].update = 1;
    uint64_t timer_counter_value = 
        ((uint64_t) TIMERG0.hw_timer[TIMER_IDX].cnt_high) << 32
        | TIMERG0.hw_timer[TIMER_IDX].cnt_low;

    /* Clear the interrupt
       and update the alarm time for the timer with without reload */
    if (intr_status & BIT(TIMER_IDX)) {
        TIMERG0.int_clr_timers.t1 = 1;
    }

    TIMERG0.hw_timer[TIMER_IDX].config.alarm_en = TIMER_ALARM_EN;

    interrupt_event_t *evt;
    evt = malloc(sizeof(interrupt_event_t));
    evt->timer_group = TIMER_GROUP_0;
    evt->timer_idx = TIMER_IDX;
    evt->timer_counter_value = timer_counter_value;
    evt->type = TIMER_INTERRUPT;

    xQueueSendFromISR(timer_queue, &evt, NULL);
}

void tg0_timer_init(double timer_interval_sec, xQueueHandle *queue)
{
    timer_queue = queue;
    /* Select and initialize basic parameters of the timer */
    timer_config_t config = 
    {
        .divider = TIMER_DIVIDER,
        .counter_dir = TIMER_COUNT_UP,
        .counter_en = TIMER_PAUSE,
        .alarm_en = TIMER_ALARM_EN,
        .intr_type = TIMER_INTR_LEVEL,
        .auto_reload = TIMER_AUTORELOAD_EN
    };
    ESP_ERROR_CHECK(timer_init(TIMER_GROUP_0, TIMER_IDX, &config));

    /* Timer's counter will initially start from value below.
       Also, if auto_reload is set, this value will be automatically reload on alarm */
    ESP_ERROR_CHECK(timer_set_counter_value(TIMER_GROUP_0, TIMER_IDX, 0x00000000ULL));

    /* Configure the alarm value and the interrupt on alarm. */
    ESP_ERROR_CHECK(timer_set_alarm_value(TIMER_GROUP_0, TIMER_IDX, timer_interval_sec * TIMER_SCALE));
    ESP_ERROR_CHECK(timer_enable_intr(TIMER_GROUP_0, TIMER_IDX));
    ESP_ERROR_CHECK(timer_isr_register(TIMER_GROUP_0, TIMER_IDX, timer_isr, (void *)TIMER_IDX, ESP_INTR_FLAG_IRAM, NULL));
    ESP_ERROR_CHECK(timer_start(TIMER_GROUP_0, TIMER_IDX));
}
