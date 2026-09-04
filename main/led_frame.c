#include "led_frame.h"
#include <string.h>

bool led_mode_animated(display_mode_t mode)
{
    return mode == DISPLAY_PLAY || mode == DISPLAY_CONNECTING || mode == DISPLAY_UNKNOWN ||
           mode == DISPLAY_AUTHENTICATING || mode == DISPLAY_DO_NOT_DISTURB;
}

bool led_frame(display_mode_t mode, uint64_t elapsed_ms, unsigned brightness_percent,
               led_rgb_t output[STATUS_LED_COUNT])
{
    if (!output || mode < 0 || mode >= DISPLAY_MODE_COUNT || brightness_percent > 100)
        return false;
    memset(output, 0, sizeof(*output) * STATUS_LED_COUNT);
    if (mode != DISPLAY_PLAY && led_mode_animated(mode) && (elapsed_ms / 750) % 2)
        return true;
    led_rgb_t color = {0};
    switch (mode) {
    case DISPLAY_AVAILABLE:
        color.green = 140;
        break;
    case DISPLAY_BUSY:
        color = (led_rgb_t){255, 255, 0};
        break;
    case DISPLAY_DO_NOT_DISTURB:
        color.red = 180;
        break;
    case DISPLAY_AUTHENTICATING:
        color = (led_rgb_t){255, 255, 0};
        break;
    case DISPLAY_CONFIG_ERROR:
        color = (led_rgb_t){180, 0, 180};
        break;
    case DISPLAY_PLAY: {
        unsigned angle = (elapsed_ms / 15) % 360;
        if (angle < 120)
            color = (led_rgb_t){(120 - angle) * 255 / 120, angle * 255 / 120, 0};
        else if (angle < 240)
            color = (led_rgb_t){0, (240 - angle) * 255 / 120, (angle - 120) * 255 / 120};
        else
            color = (led_rgb_t){(angle - 240) * 255 / 120, 0, (360 - angle) * 255 / 120};
        /* Preserve the original intended 30% rainbow intensity. */
        color.red = color.red * 30 / 100;
        color.green = color.green * 30 / 100;
        color.blue = color.blue * 30 / 100;
        break;
    }
    default:
        color.blue = 140;
        break;
    }
    color.red = color.red * brightness_percent / 100;
    color.green = color.green * brightness_percent / 100;
    color.blue = color.blue * brightness_percent / 100;
    output[0] = color;
    if (mode != DISPLAY_BUSY)
        output[1] = color;
    return true;
}
