#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Single-threaded harness: the target's critical sections are compiled by IDF. */
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
#include "../main/web_server.c"
const char test_page[] __asm__("_binary_auth_html_start") = "<!doctype html>";
static int64_t now;
static int registrations, stops, fail_registration;
static bool fail_start, no_store;
static char output[2048];
static const char *request_token, *request_body;
static size_t body_offset;
static int error_status;
void esp_fill_random(void *buffer, size_t length)
{
    memset(buffer, 42, length);
}
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *req, const char *name, char *value, size_t size)
{
    assert(!strcmp(name, "X-Frame-Token"));
    if (!request_token || strlen(request_token) >= size)
        return ESP_FAIL;
    strcpy(value, request_token);
    return ESP_OK;
}
int httpd_req_recv(httpd_req_t *req, char *buffer, size_t size)
{
    if (!request_body)
        return -1;
    size_t n = strlen(request_body) - body_offset;
    if (n > size)
        n = size;
    if (n > 3)
        n = 3; /* Exercise split request bodies. */
    memcpy(buffer, request_body + body_offset, n);
    body_offset += n;
    return n;
}
static auth_observer_t callback;
int64_t esp_timer_get_time(void)
{
    return now;
}
void auth_client_observe(auth_observer_t cb)
{
    callback = cb;
}
esp_err_t httpd_start(httpd_handle_t *handle, const httpd_config_t *config)
{
    assert(config->max_open_sockets == 3 && config->lru_purge_enable);
    assert(config->recv_wait_timeout == 5 && config->send_wait_timeout == 5);
    *handle = (void *)1;
    return fail_start ? ESP_FAIL : ESP_OK;
}
esp_err_t httpd_stop(httpd_handle_t handle)
{
    ++stops;
    return ESP_OK;
}
esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *route)
{
    assert(route->method == HTTP_GET || route->method == HTTP_POST);
    ++registrations;
    return registrations == fail_registration ? ESP_FAIL : ESP_OK;
}
esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *name, const char *value)
{
    if (!strcmp(name, "Cache-Control"))
        no_store = !strcmp(value, "no-store");
    return ESP_OK;
}
esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type)
{
    return ESP_OK;
}
esp_err_t httpd_resp_send(httpd_req_t *req, const char *body, int length)
{
    assert(length == HTTPD_RESP_USE_STRLEN);
    snprintf(output, sizeof(output), "%s", body);
    return ESP_OK;
}
esp_err_t httpd_resp_send_err(httpd_req_t *req, int status, const char *body)
{
    error_status = status;
    return ESP_FAIL;
}
static void check(const char *expected, const char *code, int seconds)
{
    no_store = false;
    assert(status_get(NULL) == ESP_OK && no_store);
    cJSON *json = cJSON_Parse(output);
    assert(json && cJSON_GetArraySize(json) == 3);
    assert(!strcmp(cJSON_GetObjectItem(json, "state")->valuestring, expected));
    assert(!strcmp(cJSON_GetObjectItem(json, "user_code")->valuestring, code));
    assert(cJSON_GetObjectItem(json, "expires_in")->valueint == seconds);
    cJSON_Delete(json);
}
static void dashboard_and_controls(void)
{
    app_status_t current = {.service = SERVICE_READY,
                            .presence = PRESENCE_AVAILABLE,
                            .has_presence = true,
                            .updated_at_us = now};
    strcpy(current.activity, "Available");
    web_server_update(&current, true, DISPLAY_AVAILABLE);
    assert(dashboard_get(NULL) == ESP_OK && no_store);
    cJSON *json = cJSON_Parse(output);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(json, "fresh")));
    assert(!strcmp(json_string(json, "activity"), "Available"));
    assert(strlen(json_string(json, "control_token")) == 32);
    cJSON_Delete(json);
    now += 60000000;
    assert(dashboard_get(NULL) == ESP_OK);
    json = cJSON_Parse(output);
    assert(cJSON_IsFalse(cJSON_GetObjectItem(json, "fresh")));
    cJSON_Delete(json);
    current.service = SERVICE_ERROR;
    current.error = SERVICE_ERROR_PERMISSION;
    web_server_update(&current, true, DISPLAY_UNKNOWN);
    assert(dashboard_get(NULL) == ESP_OK);
    json = cJSON_Parse(output);
    assert(!strcmp(json_string(json, "error"), "permission"));
    cJSON_Delete(json);
    httpd_req_t req = {.content_len = 14};
    request_body = "{\"mode\":\"red\"}";
    assert(led_test_post(&req) == ESP_FAIL && error_status == 403);
    request_token = "wrong";
    assert(led_test_post(&req) == ESP_FAIL && error_status == 403);
    request_token = control_token;
    req.content_len = strlen(request_body);
    body_offset = 0;
    assert(led_test_post(&req) == ESP_OK);
    assert(web_server_display(DISPLAY_AVAILABLE, now) == DISPLAY_DO_NOT_DISTURB);
    assert(web_server_display(DISPLAY_AVAILABLE, now + 9999999) == DISPLAY_DO_NOT_DISTURB);
    assert(web_server_display(DISPLAY_AVAILABLE, now + 10000000) == DISPLAY_AVAILABLE);
    request_body = "{\"mode\":\"auto\"}";
    req.content_len = strlen(request_body);
    body_offset = 0;
    assert(led_test_post(&req) == ESP_OK &&
           web_server_display(DISPLAY_AVAILABLE, now) == DISPLAY_AVAILABLE);
    request_body = "{\"mode\":\"invalid\"}";
    req.content_len = strlen(request_body);
    body_offset = 0;
    assert(led_test_post(&req) == ESP_FAIL && error_status == 400);
    req.content_len = 65;
    assert(led_test_post(&req) == ESP_FAIL && error_status == 400);
    req.content_len = 10;
    request_body = NULL;
    assert(led_test_post(&req) == ESP_FAIL && error_status == 400);
}

int main(void)
{
    fail_start = true;
    assert(web_server_start() == ESP_FAIL && !callback && !stops);
    fail_start = false;
    fail_registration = 2;
    assert(web_server_start() == ESP_FAIL && !callback && stops == 1);
    registrations = 0;
    fail_registration = 0;
    assert(web_server_start() == ESP_OK && registrations == 4 && callback);
    check("waiting", "", 0);
    char borrowed[] = "ABCD-EFGH";
    callback(AUTH_CODE_READY, borrowed, 5000000);
    borrowed[0] = 'X';
    check("code", "ABCD-EFGH", 5);
    now = 4999999;
    check("code", "ABCD-EFGH", 1);
    now = 5000000;
    check("retrying", "", 0);
    callback(AUTH_SIGNED_IN, NULL, 0);
    check("signed_in", "", 0);
    callback(AUTH_RETRYING, NULL, 0);
    check("retrying", "", 0);
    callback(AUTH_CODE_READY, "\"<script>", now + 1000000);
    check("code", "\"<script>", 1);
    assert(index_get(NULL) == ESP_OK && !strcmp(output, test_page));
    dashboard_and_controls();
    puts("web routes, startup cleanup, code ownership, expiry, and JSON tests passed");
}
