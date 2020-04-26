#include <stddef.h>
#include <stdbool.h>
#include "led_strip.h"

#define LED_STRIP_LENGTH 2U
#define LED_STRIP_RMT_INTR_NUM 19U

typedef struct {
    struct led_color_t colors[LED_STRIP_LENGTH];
    int interval;
} led_blink_options_t;

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

static struct led_color_t LED_COLOR_OFF = {
    .red = 0,
    .green = 0,
    .blue = 0
};

static struct led_color_t LED_COLOR_YELLOW = {
    .red = 255,
    .green = 255,
    .blue = 0
};

static struct led_color_t LED_COLOR_RED = {
    .red = 180,
    .green = 0,
    .blue = 0
};

static struct led_color_t LED_COLOR_GREEN = {
    .red = 0,
    .green = 140,
    .blue = 0
};

bool leds_init();
void leds_clear();
void led_color(int led_number, struct led_color_t *color);
void leds_color(struct led_color_t *color);
void leds_rainbow();
void leds_apply(bool flash);