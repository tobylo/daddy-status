#ifndef _LEDCONTROL_H_
#define _LEDCONTROL_H_

#include <stddef.h>
#include <stdbool.h>
#include "led_strip.h"

#define LED_STRIP_LENGTH 2U
#define LED_STRIP_RMT_INTR_NUM 19U

typedef struct {
    struct led_color_t colors[LED_STRIP_LENGTH];
    int interval;
} led_blink_options_t;

struct led_strip_t led_strip;
struct led_color_t LED_COLOR_OFF;
struct led_color_t LED_COLOR_YELLOW;
struct led_color_t LED_COLOR_RED;
struct led_color_t LED_COLOR_GREEN;

bool leds_init();
void leds_clear();
void led_color(int led_number, struct led_color_t *color);
void leds_color(struct led_color_t *color);
void leds_rainbow();
void leds_apply(bool flash);

#endif