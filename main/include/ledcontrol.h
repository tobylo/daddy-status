#ifndef DADDY_LEDCONTROL_H
#define DADDY_LEDCONTROL_H
#include "app_state.h"
#include "esp_err.h"
esp_err_t leds_init(void);
esp_err_t leds_set_mode(display_mode_t mode);
#endif
