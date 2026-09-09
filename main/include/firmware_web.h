#ifndef DADDY_FIRMWARE_WEB_H
#define DADDY_FIRMWARE_WEB_H
#include "cJSON.h"
#include "esp_http_server.h"
/* Read-only update capabilities and boot state; caller adds its per-boot token. */
cJSON *firmware_status_json(void);
/* Caller authenticates and supplies a bounded, decimal GitHub asset ID. */
esp_err_t firmware_download(httpd_req_t *req, const char *asset_id);
#endif
