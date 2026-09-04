#include "http_transport.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#define RESPONSE_CAPACITY (16384 + 1)
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static esp_err_t http_event(esp_http_client_event_t *event)
{
    http_response_t *response = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        if (!response_append(&response->body, event->data, (size_t)event->data_len)) {
            return ESP_ERR_INVALID_SIZE;
        }
    } else if (event->event_id == HTTP_EVENT_ON_HEADER &&
               strcasecmp(event->header_key, "Retry-After") == 0) {
        response->retry_after = retry_after_seconds(event->header_value);
    }
    return ESP_OK;
}

void http_response_free(http_response_t *response)
{
    secret_free(response->body.data);
    memset(response, 0, sizeof(*response));
}

esp_err_t http_request(const char *url, const char *form, const char *token,
                         http_response_t *response)
{
    memset(response, 0, sizeof(*response));
    response->body.data = calloc(RESPONSE_CAPACITY, 1);
    if (!response->body.data) return ESP_ERR_NO_MEM;
    response->body.capacity = RESPONSE_CAPACITY;
    const esp_http_client_config_t config = {
        .url = url,
        .method = form ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .event_handler = http_event,
        .user_data = response,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .disable_auto_redirect = true,
        .buffer_size = 2048,
        .buffer_size_tx = TOKEN_LIMIT + 128,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_http_client_set_header(client, "Accept", "application/json");
    char *authorization = NULL;
    if (err == ESP_OK && token) {
        if (asprintf(&authorization, "Bearer %s", token) < 0) err = ESP_ERR_NO_MEM;
        else err = esp_http_client_set_header(client, "Authorization", authorization);
    }
    if (err == ESP_OK && form) {
        err = esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
        if (err == ESP_OK) err = esp_http_client_set_post_field(client, form, strlen(form));
    }
    if (err == ESP_OK) err = esp_http_client_perform(client);
    response->status = esp_http_client_get_status_code(client);
    if (err == ESP_OK && (response->body.overflow || !esp_http_client_is_complete_data_received(client))) {
        err = ESP_ERR_INVALID_SIZE;
    }
    esp_http_client_cleanup(client);
    secret_free(authorization);
    return err;
}

