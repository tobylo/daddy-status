#include <stddef.h>
#include "led_strip.h"

#define LED_STRIP_LENGTH 2U
#define LED_STRIP_RMT_INTR_NUM 19U

static struct led_color_t led_strip_buf_1[LED_STRIP_LENGTH];
static struct led_color_t led_strip_buf_2[LED_STRIP_LENGTH];
static struct led_strip_t led_strip = {
    .rgb_led_type = RGB_LED_TYPE_WS2812,
    .rmt_channel = RMT_CHANNEL_1,
    .rmt_interrupt_num = LED_STRIP_RMT_INTR_NUM,
    .gpio = CONFIG_LED_DATA_GPIO,
    .led_strip_buf_1 = led_strip_buf_1,
    .led_strip_buf_2 = led_strip_buf_2,
    .led_strip_length = LED_STRIP_LENGTH
};

static struct led_color_t LED_COLOR_RED = {
    .red = 255,
    .green = 0,
    .blue = 0
};

bool leds_init();
void leds_blink_random();
void leds_rainbow();

void leds_blink(struct led_color_t *color);
static inline void leds_blink_alert() { leds_blink(&LED_COLOR_RED); }

void leds_clear();

void leds_rainbow_task(void *pvParameters);