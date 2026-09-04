#ifndef TEST_LED_STRIP_H
#define TEST_LED_STRIP_H
#include "esp_err.h"
#include <stdint.h>
typedef void *led_strip_handle_t;
#define LED_MODEL_WS2812 0
typedef struct { int strip_gpio_num; unsigned max_leds; int led_model; } led_strip_config_t;
typedef struct { unsigned resolution_hz; } led_strip_rmt_config_t;
esp_err_t led_strip_new_rmt_device(const led_strip_config_t *config,
                                  const led_strip_rmt_config_t *rmt, led_strip_handle_t *strip);
esp_err_t led_strip_set_pixel(led_strip_handle_t strip, unsigned index,
                             uint32_t red, uint32_t green, uint32_t blue);
esp_err_t led_strip_refresh(led_strip_handle_t strip);
esp_err_t led_strip_del(led_strip_handle_t strip);
#endif
