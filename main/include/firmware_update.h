#ifndef DADDY_FIRMWARE_UPDATE_H
#define DADDY_FIRMWARE_UPDATE_H
#include "esp_http_server.h"
#include <stdbool.h>
#include <stdint.h>
/* Called after local startup succeeds, then by the main loop. */
void firmware_update_init(void);
void firmware_update_tick(bool connected, int64_t now);
/* Caller checks the per-boot request token before accepting the binary body. */
esp_err_t firmware_update_upload(httpd_req_t *req);
#endif
