#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_types.h"
#include "soc/timer_group_struct.h"
#include "driver/timer.h"

void tg0_timer_init(double timer_interval_sec, xQueueHandle *queue);