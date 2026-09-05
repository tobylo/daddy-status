#ifndef TEST_HTTP_SERVER_H
#define TEST_HTTP_SERVER_H
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
typedef void *httpd_handle_t;
typedef struct {
    int unused;
} httpd_req_t;
typedef struct {
    const char *uri;
    int method;
    esp_err_t (*handler)(httpd_req_t *);
} httpd_uri_t;
typedef struct {
    int max_open_sockets;
    bool lru_purge_enable;
    int recv_wait_timeout, send_wait_timeout;
} httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
#define HTTP_GET 0
#define HTTPD_RESP_USE_STRLEN -1
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
esp_err_t httpd_start(httpd_handle_t *, const httpd_config_t *);
esp_err_t httpd_stop(httpd_handle_t);
esp_err_t httpd_register_uri_handler(httpd_handle_t, const httpd_uri_t *);
esp_err_t httpd_resp_set_hdr(httpd_req_t *, const char *, const char *);
esp_err_t httpd_resp_set_type(httpd_req_t *, const char *);
esp_err_t httpd_resp_send(httpd_req_t *, const char *, int);
esp_err_t httpd_resp_send_err(httpd_req_t *, int, const char *);
#endif
