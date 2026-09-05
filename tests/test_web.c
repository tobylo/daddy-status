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
static char output[512];
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
    assert(route->method == HTTP_GET);
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
int main(void)
{
    fail_start = true;
    assert(web_server_start() == ESP_FAIL && !callback && !stops);
    fail_start = false;
    fail_registration = 2;
    assert(web_server_start() == ESP_FAIL && !callback && stops == 1);
    registrations = 0;
    fail_registration = 0;
    assert(web_server_start() == ESP_OK && registrations == 2 && callback);
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
    puts("web routes, startup cleanup, code ownership, expiry, and JSON tests passed");
}
