#ifndef DADDY_LEDCONTROL_H
#define DADDY_LEDCONTROL_H
#include "app_state.h"
#include "esp_err.h"
esp_err_t leds_init(void);
esp_err_t leds_set_mode(display_mode_t mode);
/* Wake the worker to redraw live brightness without changing its display mode. */
void leds_refresh(void);
#endif
