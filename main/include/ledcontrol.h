#ifndef _LEDCONTROL_H_
#define _LEDCONTROL_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

struct led_color_t { uint8_t red, green, blue; };

#define LED_STRIP_LENGTH 2U

typedef struct {
    struct led_color_t colors[LED_STRIP_LENGTH];
    int interval;
} led_blink_options_t;

extern struct led_color_t LED_COLOR_OFF;
extern struct led_color_t LED_COLOR_YELLOW;
extern struct led_color_t LED_COLOR_RED;
extern struct led_color_t LED_COLOR_GREEN;

bool leds_init(void);
void leds_clear(void);
void led_color(int led_number, struct led_color_t *color);
void leds_color(struct led_color_t *color);
void leds_rainbow(void);
void leds_apply(bool flash);

#endif