#include <stdio.h>
#include "driver/gpio.h"
#include "esp_err.h"

#define _GPIO_NUMBER(num) GPIO_NUM_##num
#define GPIO_NUMBER(num) _GPIO_NUMBER(num)

#define GPS_INT_INPUT_PIN CONFIG_GNSS_GPIO_INTERRUPT_PIN
#define GPS_INTERRUPT_PIN GPIO_NUMBER(GPS_INT_INPUT_PIN)

esp_err_t gpio_interrupt_setup(gpio_num_t gpio_pin, GPIO_INT_TYPE trigger_type, gpio_isr_t handler);