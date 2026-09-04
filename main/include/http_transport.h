#ifndef DADDY_HTTP_TRANSPORT_H
#define DADDY_HTTP_TRANSPORT_H
#include "esp_err.h"
#include "protocol.h"

typedef struct {
    response_buffer_t body;
    int status;
    unsigned retry_after;
} http_response_t;

/* Always release response with http_response_free, including on error. */
esp_err_t http_request(const char *url, const char *form, const char *token,
                       http_response_t *response);
void http_response_free(http_response_t *response);
#endif
