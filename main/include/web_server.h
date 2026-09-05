#ifndef DADDY_WEB_SERVER_H
#define DADDY_WEB_SERVER_H
#include "esp_err.h"
/* Start after Wi-Fi initialization, before the authentication worker. */
esp_err_t web_server_start(void);
#endif
