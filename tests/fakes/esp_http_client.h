#ifndef TEST_HTTP_CLIENT_H
#define TEST_HTTP_CLIENT_H
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
typedef void *esp_http_client_handle_t;
typedef enum { HTTP_EVENT_ON_DATA, HTTP_EVENT_ON_HEADER } esp_http_client_event_id_t;
typedef enum { HTTP_METHOD_POST, HTTP_METHOD_GET } esp_http_client_method_t;
typedef struct {
    esp_http_client_event_id_t event_id;
    void *user_data;
    void *data;
    int data_len;
    char *header_key, *header_value;
} esp_http_client_event_t;
typedef struct {
    const char *url;
    esp_http_client_method_t method;
    esp_err_t (*event_handler)(esp_http_client_event_t *);
    void *user_data;
    esp_err_t (*crt_bundle_attach)(void *);
    int timeout_ms;
    bool disable_auto_redirect;
    int buffer_size, buffer_size_tx;
} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key,
                                     const char *value);
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client, const char *form,
                                         int length);
esp_err_t esp_http_client_perform(esp_http_client_handle_t client);
int esp_http_client_get_status_code(esp_http_client_handle_t client);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t client);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);
#endif
