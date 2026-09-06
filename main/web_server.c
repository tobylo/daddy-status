#include "web_server.h"
#include "auth_client.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "protocol.h"
#include "sdkconfig.h"
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
    auth_event_t event;
    char code[33];
    int64_t deadline;
} state;
static app_status_t app = {.service = SERVICE_CONNECTING, .presence = PRESENCE_UNKNOWN};
static bool connected;
static display_mode_t displayed = DISPLAY_CONNECTING, test_mode;
static int64_t test_deadline;
static char control_token[33];

void web_server_update(const app_status_t *status, bool online, display_mode_t display)
{
    portENTER_CRITICAL(&lock);
    app = *status;
    connected = online;
    displayed = display;
    portEXIT_CRITICAL(&lock);
}

display_mode_t web_server_display(display_mode_t normal, int64_t now_us)
{
    portENTER_CRITICAL(&lock);
    display_mode_t mode = now_us < test_deadline ? test_mode : normal;
    portEXIT_CRITICAL(&lock);
    return mode;
}

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

static cJSON *auth_json(void)
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
        return NULL;
    }
    return json;
}

/* Takes ownership, including on allocation or socket failures. */
static esp_err_t send_json(httpd_req_t *req, cJSON *json)
{
    char *body = json ? cJSON_PrintUnformatted(json) : NULL;
    cJSON_Delete(json);
    headers(req);
    if (!body)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    free(body);
    return err;
}

static esp_err_t status_get(httpd_req_t *req)
{
    return send_json(req, auth_json());
}

static const char *label(const char *const *names, unsigned count, unsigned value)
{
    return value < count ? names[value] : "unknown";
}
#define LABEL(names, value) label(names, sizeof(names) / sizeof(names[0]), value)

static esp_err_t dashboard_get(httpd_req_t *req)
{
    app_status_t current;
    bool online;
    display_mode_t display;
    int64_t until;
    portENTER_CRITICAL(&lock);
    current = app;
    online = connected;
    display = displayed;
    until = test_deadline;
    portEXIT_CRITICAL(&lock);
    int64_t now = esp_timer_get_time();
    int64_t age =
        current.has_presence && now >= current.updated_at_us ? now - current.updated_at_us : -1;
    const char *const services[] = {"connecting", "clock", "authenticating", "polling",
                                    "ready",      "error", "configuration"};
    const char *const errors[] = {"none",        "network",     "clock",        "auth",
                                  "auth_config", "auth_denied", "auth_expired", "storage",
                                  "permission",  "throttled",   "response"};
    const char *const displays[] = {"connecting", "authenticating", "unknown",
                                    "green",      "yellow",         "red",
                                    "rainbow",    "configuration"};
    cJSON *json = auth_json();
    if (!json || !cJSON_AddBoolToObject(json, "connected", online) ||
        !cJSON_AddStringToObject(json, "service", LABEL(services, current.service)) ||
        !cJSON_AddStringToObject(json, "error", LABEL(errors, current.error)) ||
        !cJSON_AddStringToObject(json, "activity", current.has_presence ? current.activity : "") ||
        !cJSON_AddStringToObject(json, "display", LABEL(displays, display)) ||
        !cJSON_AddNumberToObject(json, "age_seconds", age < 0 ? -1 : age / 1000000) ||
        !cJSON_AddBoolToObject(json, "fresh",
                               online && age >= 0 &&
                                   age < (int64_t)settings_get()->stale_seconds * 1000000) ||
        !cJSON_AddNumberToObject(json, "poll_seconds", settings_get()->poll_seconds) ||
        !cJSON_AddNumberToObject(json, "uptime_seconds", now / 1000000) ||
        !cJSON_AddNumberToObject(json, "led_gpio", CONFIG_LED_DATA_GPIO) ||
        !cJSON_AddNumberToObject(json, "brightness_percent", settings_get()->brightness) ||
        !cJSON_AddNumberToObject(json, "test_seconds",
                                 until > now ? (until - now + 999999) / 1000000 : 0) ||
        !cJSON_AddStringToObject(json, "control_token", control_token)) {
        cJSON_Delete(json);
        json = NULL;
    }
    return send_json(req, json);
}

static esp_err_t led_test_post(httpd_req_t *req)
{
    headers(req);
    char supplied[sizeof(control_token)];
    if (httpd_req_get_hdr_value_str(req, "X-Frame-Token", supplied, sizeof(supplied)) != ESP_OK ||
        !control_token[0] || strcmp(supplied, control_token))
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Reload the frame page");
    char body[65];
    if (req->content_len <= 0 || req->content_len >= sizeof(body))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid test request");
    size_t received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n <= 0)
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incomplete request");
        received += n;
    }
    body[received] = '\0';
    response_buffer_t buffer = {.data = body, .length = received, .capacity = sizeof(body)};
    cJSON *json = response_json(&buffer);
    const char *name = json_string(json, "mode");
    display_mode_t mode = DISPLAY_MODE_COUNT;
    bool automatic = name && !strcmp(name, "auto");
    if (name && !strcmp(name, "green"))
        mode = DISPLAY_AVAILABLE;
    else if (name && !strcmp(name, "yellow"))
        mode = DISPLAY_BUSY;
    else if (name && !strcmp(name, "red"))
        mode = DISPLAY_DO_NOT_DISTURB;
    else if (name && !strcmp(name, "blue"))
        mode = DISPLAY_UNKNOWN;
    else if (name && !strcmp(name, "rainbow"))
        mode = DISPLAY_PLAY;
    cJSON_Delete(json);
    if (!automatic && mode == DISPLAY_MODE_COUNT)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown test mode");
    int64_t until = automatic ? 0 : esp_timer_get_time() + 10000000;
    portENTER_CRITICAL(&lock);
    test_mode = mode;
    test_deadline = until;
    portEXIT_CRITICAL(&lock);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t settings_get_handler(httpd_req_t *req)
{
    return send_json(req, settings_json());
}

static esp_err_t settings_post_handler(httpd_req_t *req)
{
    headers(req);
    char supplied[sizeof(control_token)];
    if (httpd_req_get_hdr_value_str(req, "X-Frame-Token", supplied, sizeof(supplied)) != ESP_OK ||
        !control_token[0] || strcmp(supplied, control_token))
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Reload the frame page");
    char body[1537];
    if (!req->content_len || req->content_len >= sizeof(body))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid settings request");
    size_t received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n <= 0)
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incomplete request");
        received += n;
    }
    body[received] = 0;
    response_buffer_t buffer = {.data = body, .length = received, .capacity = sizeof(body)};
    cJSON *json = response_json(&buffer);
    frame_settings_t value;
    const char *action = json_string(json, "action");
    bool reset = action && !strcmp(action, "reset_auth");
    bool valid = reset || (action && !strcmp(action, "save") && settings_parse(json, &value));
    cJSON_Delete(json);
    memset(body, 0, sizeof(body));
    if (!valid)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Check settings fields and ranges");
    esp_err_t err = reset ? settings_reset_auth() : settings_save(&value);
    memset(&value, 0, sizeof(value));
    if (err != ESP_OK)
        return httpd_resp_send_err(
            req, HTTPD_500_INTERNAL_SERVER_ERROR,
            "Settings not confirmed: storage failure or restart/trial in progress");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

esp_err_t web_server_start(void)
{
    unsigned char random[16];
    esp_fill_random(random, sizeof(random));
    for (size_t i = 0; i < sizeof(random); ++i)
        snprintf(control_token + 2 * i, 3, "%02x", random[i]);
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
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
        {.uri = "/api/status", .method = HTTP_GET, .handler = dashboard_get},
        {.uri = "/api/settings", .method = HTTP_GET, .handler = settings_get_handler},
        {.uri = "/api/settings", .method = HTTP_POST, .handler = settings_post_handler},
        {.uri = "/api/led-test", .method = HTTP_POST, .handler = led_test_post},
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
