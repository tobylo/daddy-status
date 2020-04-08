#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "ledcontrol.h"
#include "gpio.h"
#include "interrupt.h"
#include "timer.h"

#define ESP_INTR_FLAG_DEFAULT 0

static const char* TAG = "main";
static xQueueHandle evt_queue = NULL;

void app_main()
{

    init_leds();
    //leds_blink_random();
    //leds_rainbow();
    leds_blink_alert();

    ESP_LOGI(TAG, "All initialized");
    
    /*
    while(1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        gps_retrieve();
    }
    */
}