#include "web_server.h"
#include "auth_client.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
    auth_event_t event;
    char code[33];
    int64_t deadline;
} state;
extern const char page[] __asm__("_binary_auth_html_start");

static void observe(auth_event_t event, const char *code, int64_t deadline)
{
    portENTER_CRITICAL(&lock);
    state.event = event;
    snprintf(state.code, sizeof(state.code), "%s", code ? code : "");
    state.deadline = deadline;
    portEXIT_CRITICAL(&lock);
}

static void headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(
        req, "Content-Security-Policy",
        "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
        "connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
}

static esp_err_t index_get(httpd_req_t *req)
{
    headers(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_get(httpd_req_t *req)
{
    char code[33];
    auth_event_t event;
    int64_t deadline;
    portENTER_CRITICAL(&lock);
    memcpy(code, state.code, sizeof(code));
    event = state.event;
    deadline = state.deadline;
    portEXIT_CRITICAL(&lock);
    int64_t remaining = deadline - esp_timer_get_time();
    if (event == AUTH_CODE_READY && remaining <= 0) {
        event = AUTH_RETRYING;
        code[0] = '\0';
    }
    const char *names[] = {"waiting", "code", "signed_in", "retrying"};
    cJSON *json = cJSON_CreateObject();
    if (!json || !cJSON_AddStringToObject(json, "state", names[event]) ||
        !cJSON_AddStringToObject(json, "user_code", code) ||
        !cJSON_AddNumberToObject(json, "expires_in",
                                 event == AUTH_CODE_READY ? (remaining + 999999) / 1000000 : 0)) {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!body)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    headers(req);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    free(body);
    return err;
}

esp_err_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 3;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 5;
    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK)
        return err;
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = index_get},
        {.uri = "/api/auth", .method = HTTP_GET, .handler = status_get},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) {
            httpd_stop(server);
            return err;
        }
    }
    auth_client_observe(observe);
    return ESP_OK;
}
