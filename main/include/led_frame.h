#ifndef DADDY_LED_FRAME_H
#define DADDY_LED_FRAME_H
#include "app_state.h"
#define STATUS_LED_COUNT 2

typedef struct {
    uint8_t red, green, blue;
} led_rgb_t;
bool led_frame(display_mode_t mode, uint64_t elapsed_ms, unsigned brightness_percent,
               led_rgb_t output[STATUS_LED_COUNT]);
bool led_mode_animated(display_mode_t mode);
#endif
