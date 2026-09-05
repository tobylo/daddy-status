#ifndef DADDY_WEB_SERVER_H
#define DADDY_WEB_SERVER_H
#include "app_state.h"
#include "esp_err.h"
/* Start after Wi-Fi initialization, before the authentication worker. */
esp_err_t web_server_start(void);
/* Main task publishes a value snapshot; server never accesses the worker queue. */
void web_server_update(const app_status_t *status, bool connected, display_mode_t display);
/* Main remains the only producer of LED modes; tests expire on the device clock. */
display_mode_t web_server_display(display_mode_t normal, int64_t now_us);
#endif
