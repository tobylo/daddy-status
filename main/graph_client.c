#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "tcpip_adapter.h"
#include "protocol_examples_common.h"
#include "esp_tls.h"
#include "esp_http_client.h"

#define MAX_HTTP_RECV_BUFFER 512
static const char *TAG = "HTTP_CLIENT";

static const char *TENANT_ID = CONFIG_AAD_TENANT_ID;
static const char *CLIENT_ID = CONFIG_AAD_CLIENT_ID;

esp_err_t graph_client_init()
{
    return refresh_token();
}

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // Write out data
                // printf("%.*s", evt->data_len, (char*)evt->data);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            break;
    }
    return ESP_OK;
}

static esp_err_t refresh_token()
{
    // TODO: Verify if refresh token exists in spiffy, if so, try to fetch new access token
    // If successful, store new refresh token, otherwise continue

    char *url = NULL;
    asprintf(&url, "https://login.microsoftonline.com/%s/oauth2/v2.0/devicecode", TENANT_ID);
    esp_http_client_config_t config = {
        .url = *url,
        .event_handler = _http_event_handler
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    char *data = NULL;
    asprintf(&data, "client_id=%s&scope=presence.read%20offline_access", "something");
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client, data, strlen(data));
    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        char *response = malloc(sizeof(char) * 512);
        int response_length = 0;
        response_length = esp_http_client_read_response(client, response, 512);
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d, content = %s",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client),
                response);
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    // TODO: Poll for new token when user has accepted

    // TODO: Store access token and refresh token in spiffy

    // TODO: Set timer and callback to fetch new access token and store the new refresh

    return ESP_OK;
}