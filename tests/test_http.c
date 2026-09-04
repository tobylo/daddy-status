#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "http_transport.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static esp_http_client_config_t config;
static unsigned cleaned, performed;
static int scenario;
static const char *post;
static char auth_header[128];

enum { GOOD, OVERSIZED, INCOMPLETE, TRANSPORT_ERROR, INIT_ERROR, HEADER_ERROR, THROTTLED };
esp_err_t esp_crt_bundle_attach(void *unused)
{
    (void)unused;
    return ESP_OK;
}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *value)
{
    config = *value;
    assert(config.crt_bundle_attach == esp_crt_bundle_attach && config.disable_auto_redirect);
    assert(config.timeout_ms > 0 && config.timeout_ms <= 15000);
    return scenario == INIT_ERROR ? NULL : &config;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key,
                                     const char *value)
{
    assert(client == &config && value);
    if (scenario == HEADER_ERROR)
        return ESP_FAIL;
    if (!strcmp(key, "Authorization"))
        snprintf(auth_header, sizeof(auth_header), "%s", value);
    return ESP_OK;
}
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client, const char *form,
                                         int length)
{
    assert(client == &config && strlen(form) == (size_t)length);
    post = form;
    return ESP_OK;
}
esp_err_t esp_http_client_perform(esp_http_client_handle_t client)
{
    assert(client == &config);
    ++performed;
    if (scenario == TRANSPORT_ERROR)
        return ESP_FAIL;
    if (config.method == HTTP_METHOD_POST)
        assert(post && !strcmp(post, "key=value"));
    esp_http_client_event_t event = {.user_data = config.user_data,
                                     .event_id = HTTP_EVENT_ON_HEADER,
                                     .header_key = "rEtRy-AfTeR",
                                     .header_value = "120"};
    if (scenario == THROTTLED)
        assert(config.event_handler(&event) == ESP_OK);
    event.event_id = HTTP_EVENT_ON_DATA;
    if (scenario == OVERSIZED) {
        char bytes[1024];
        memset(bytes, 'x', sizeof(bytes));
        event.data = bytes;
        event.data_len = sizeof(bytes);
        for (unsigned i = 0; i < 16; ++i)
            assert(config.event_handler(&event) == ESP_OK);
        assert(config.event_handler(&event) == ESP_ERR_INVALID_SIZE);
        /* The SDK can ignore callback errors. The transport must still reject overflow. */
    } else {
        event.data = "{\"activity\":";
        event.data_len = strlen(event.data);
        assert(config.event_handler(&event) == ESP_OK);
        event.data = "\"Busy\"}";
        event.data_len = strlen(event.data);
        assert(config.event_handler(&event) == ESP_OK);
    }
    return ESP_OK;
}
int esp_http_client_get_status_code(esp_http_client_handle_t client)
{
    assert(client == &config);
    return scenario == THROTTLED ? 429 : 200;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t client)
{
    assert(client == &config);
    return scenario != INCOMPLETE;
}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client)
{
    assert(client == &config);
    ++cleaned;
    post = NULL;
    return ESP_OK;
}

int main(void)
{
    const esp_err_t expected[] = {ESP_OK,   ESP_ERR_INVALID_SIZE, ESP_ERR_INVALID_SIZE,
                                  ESP_FAIL, ESP_ERR_NO_MEM,       ESP_FAIL,
                                  ESP_OK};
    for (scenario = GOOD; scenario <= THROTTLED; ++scenario) {
        cleaned = performed = 0;
        auth_header[0] = 0;
        http_response_t response;
        assert(http_request("https://example.invalid", "key=value", "abc.DEF-123", &response) ==
               expected[scenario]);
        assert(cleaned == (scenario == INIT_ERROR ? 0 : 1));
        if (scenario != INIT_ERROR && scenario != HEADER_ERROR)
            assert(!strcmp(auth_header, "Bearer abc.DEF-123"));
        if (scenario == GOOD) {
            cJSON *root = response_json(&response.body);
            assert(root && !strcmp(json_string(root, "activity"), "Busy"));
            cJSON_Delete(root);
        }
        if (scenario == THROTTLED)
            assert(response.retry_after == 120 && response.status == 429);
        http_response_free(&response);
        assert(!response.body.data && !response.body.length);
    }
    http_response_t response;
    scenario = GOOD;
    assert(http_request("https://example.invalid", NULL, "access", &response) == ESP_OK);
    assert(config.method == HTTP_METHOD_GET && !post);
    http_response_free(&response);
    assert(http_request(NULL, NULL, NULL, &response) == ESP_ERR_INVALID_ARG);
    http_response_free(&response);
    assert(http_request("https://example.invalid", NULL, "token\r\nX-Evil: yes", &response) ==
           ESP_ERR_INVALID_ARG);
    http_response_free(&response);
    assert(http_request("https://example.invalid", NULL, NULL, NULL) == ESP_ERR_INVALID_ARG);
    http_response_free(NULL);
    puts("HTTP transport TLS configuration, chunking, overflow, failure, and cleanup tests passed");
}
