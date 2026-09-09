#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "firmware_web.h"
#include "sdkconfig.h"
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#if CONFIG_FRAME_OTA_ENABLE
/* GitHub's asset redirects lack browser CORS support. Stream through the frame,
 * without buffering the image in ESP32 RAM or writing anything to flash. */
enum { DOWNLOAD_BUFFER_SIZE = 4096, REDIRECT_CAPACITY = 2048 };
static const int64_t DOWNLOAD_TIMEOUT_US = 120000000;
static const char ASSET_ORIGIN[] = "https://release-assets.githubusercontent.com/";

typedef struct {
    char location[REDIRECT_CAPACITY];
    char buffer[DOWNLOAD_BUFFER_SIZE];
} download_buffers_t;

static esp_err_t capture_location(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_HEADER)
        return ESP_OK;
    if (strcasecmp(event->header_key, "Location"))
        return ESP_OK;
    download_buffers_t *buffers = event->user_data;
    size_t length = strlen(event->header_value);
    if (length >= sizeof(buffers->location))
        return ESP_ERR_INVALID_SIZE;
    memcpy(buffers->location, event->header_value, length + 1);
    return ESP_OK;
}

static esp_http_client_handle_t open_download(const char *url, download_buffers_t *buffers)
{
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = capture_location,
        .user_data = buffers,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .disable_auto_redirect = true,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
        return NULL;
    esp_http_client_set_header(client, "Accept", "application/octet-stream");
    esp_http_client_set_header(client, "User-Agent", "daddy-status");
    if (esp_http_client_open(client, 0) == ESP_OK && esp_http_client_fetch_headers(client) >= 0)
        return client;
    esp_http_client_cleanup(client);
    return NULL;
}

static esp_http_client_handle_t open_asset(const char *id, download_buffers_t *buffers)
{
    char url[128];
    snprintf(url, sizeof(url),
             "https://api.github.com/repos/tobylo/daddy-status/releases/assets/%s", id);
    esp_http_client_handle_t client = open_download(url, buffers);
    if (!client || esp_http_client_get_status_code(client) != 302)
        return client;
    esp_http_client_cleanup(client);
    if (strncmp(buffers->location, ASSET_ORIGIN, sizeof(ASSET_ORIGIN) - 1))
        return NULL;
    return open_download(buffers->location, buffers);
}

static esp_err_t stream_asset(httpd_req_t *req, esp_http_client_handle_t client,
                              download_buffers_t *buffers, size_t limit)
{
    int64_t expected = esp_http_client_get_content_length(client);
    if (expected <= 0 || (uint64_t)expected > limit)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Release image does not fit the OTA slot");
    httpd_resp_set_type(req, "application/octet-stream");
    int64_t deadline = esp_timer_get_time() + DOWNLOAD_TIMEOUT_US;
    size_t received = 0;
    while (received < (size_t)expected) {
        if (esp_timer_get_time() >= deadline)
            return ESP_FAIL; /* Close the incomplete chunked response. */
        int count = esp_http_client_read(client, buffers->buffer, sizeof(buffers->buffer));
        if (count <= 0 || (size_t)count > (size_t)expected - received)
            return ESP_FAIL;
        if (httpd_resp_send_chunk(req, buffers->buffer, count) != ESP_OK)
            return ESP_FAIL;
        received += count;
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t download_reserved(httpd_req_t *req, const char *id, size_t limit)
{
    download_buffers_t *buffers = calloc(1, sizeof(*buffers));
    if (!buffers)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    esp_http_client_handle_t client = open_asset(id, buffers);
    esp_err_t result;
    if (client && esp_http_client_get_status_code(client) == 200)
        result = stream_asset(req, client, buffers, limit);
    else
        result = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                     "GitHub download unavailable. Check internet, device clock or "
                                     "rate limit; try a local file.");
    if (client)
        esp_http_client_cleanup(client);
    free(buffers);
    return result;
}

#endif

esp_err_t firmware_download(httpd_req_t *req, const char *asset_id)
{
#if !CONFIG_FRAME_OTA_ENABLE
    return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Signed firmware updates are disabled");
#else
    size_t length = strlen(asset_id);
    if (!length || length > 20 || strspn(asset_id, "0123456789") != length)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid release asset ID");
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target || !settings_update_begin())
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Firmware is not ready for downloads");
    esp_err_t result = download_reserved(req, asset_id, target->size);
    settings_update_end();
    return result;
#endif
}
