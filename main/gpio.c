#include <stdio.h>
#include "esp_log.h"
#include "gpio.h"
#include "interrupt.h"

static const char* TAG = "GPIO";

esp_err_t gpio_interrupt_setup(gpio_num_t gpio_pin, GPIO_INT_TYPE trigger_type, gpio_isr_t handler)
{
    esp_err_t ret = NULL;

    // Set up gpio for interrupts when data is available
    gpio_config_t io_conf;
    io_conf.intr_type = trigger_type;
    io_conf.mode = GPIO_MODE_DEF_INPUT;
    io_conf.pin_bit_mask = (1ULL<<((int)gpio_pin));
    io_conf.pull_up_en = 0;
    io_conf.pull_down_en = 0;
    ret = gpio_config(&io_conf);

    if( ret != ESP_OK)
    {
        return ret;
    }

    // set up interrupts
    ret = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if( ret != ESP_OK)
    {
        return ret;
    }

    ret = gpio_isr_handler_add(gpio_pin, handler, (void*) gpio_pin);

    if(ret == ESP_OK)
    {
        ESP_LOGI(TAG, "GPS interrupts initialized");
    }
    else
    {
        ESP_LOGE(TAG, "GPS interrupt init failed");
    }
    
    return ret;
}