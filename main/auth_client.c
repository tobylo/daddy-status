#include "auth_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_transport.h"
#include "sdkconfig.h"
#include "settings.h"
#include "task_time.h"
#include "token_storage.h"
#include "wifi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "auth";
static auth_observer_t observer;
void auth_client_observe(auth_observer_t callback)
{
    observer = callback;
}
static void notify(auth_event_t event, const char *code, int64_t deadline)
{
    if (observer)
        observer(event, code, deadline);
}

static const char *SCOPE = "https://graph.microsoft.com/Presence.Read offline_access";

static bool error_is(const char *code, const char *expected)
{
    return code && !strcmp(code, expected);
}

static void classify_failure(auth_client_t *auth, const cJSON *root, int status)
{
    static const struct {
        const char *code;
        service_error_t error;
    } errors[] = {
        {"invalid_client", SERVICE_ERROR_AUTH_CONFIG},
        {"unauthorized_client", SERVICE_ERROR_AUTH_CONFIG},
        {"invalid_scope", SERVICE_ERROR_AUTH_CONFIG},
        {"authorization_declined", SERVICE_ERROR_AUTH_DENIED},
        {"access_denied", SERVICE_ERROR_AUTH_DENIED},
        {"expired_token", SERVICE_ERROR_AUTH_EXPIRED},
    };
    if (status == 429) {
        auth->error = SERVICE_ERROR_THROTTLED;
        return;
    }
    const char *code = json_string(root, "error");
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        if (error_is(code, errors[i].code)) {
            auth->error = errors[i].error;
            return;
        }
    }
}

static bool refresh_token_valid(const char *refresh, bool refreshing)
{
    if (!refresh)
        return refreshing;
    return strlen(refresh) <= TOKEN_LIMIT;
}

static bool token_response_valid(const cJSON *root, bool refreshing, unsigned *expires)
{
    const char *type = json_string(root, "token_type");
    if (!type || strcasecmp(type, "Bearer"))
        return false;
    return bearer_token_valid(json_string(root, "access_token")) &&
           refresh_token_valid(json_string(root, "refresh_token"), refreshing) &&
           json_seconds(root, "expires_in", expires);
}

static esp_err_t accept_tokens(auth_client_t *auth, const cJSON *root, bool refreshing)
{
    const char *access = json_string(root, "access_token");
    const char *refresh = json_string(root, "refresh_token");
    unsigned expires;
    if (!token_response_valid(root, refreshing, &expires))
        return ESP_ERR_INVALID_RESPONSE;
    char *copy = strdup(access);
    if (!copy)
        return ESP_ERR_NO_MEM;
    esp_err_t err = refresh ? token_storage_write(refresh) : ESP_OK;
    if (err != ESP_OK) {
        auth->error = SERVICE_ERROR_STORAGE;
        secret_free(copy);
        return err;
    }
    secret_free(auth->access_token);
    auth->access_token = copy;
    unsigned margin = expires > 120 ? 60 : expires / 2;
    auth->access_deadline = esp_timer_get_time() + (int64_t)(expires - margin) * 1000000;
    return ESP_OK;
}

static esp_err_t auth_request(const char *path, const char *form, http_response_t *response)
{
    char url[160];
    int n = snprintf(url, sizeof(url), "https://login.microsoftonline.com/%s/oauth2/v2.0/%s",
                     settings_get()->tenant, path);
    if (n < 0 || (size_t)n >= sizeof(url)) {
        memset(response, 0, sizeof(*response));
        return ESP_ERR_INVALID_SIZE;
    }
    return http_request(url, form, NULL, response);
}

static char *token_form(const char *value, bool refreshing)
{
    char *encoded = form_encode(value);
    if (!encoded)
        return NULL;
    char *form = NULL;
    int n = asprintf(&form, "client_id=%s&grant_type=%s&%s=%s", settings_get()->client,
                     refreshing ? "refresh_token"
                                : "urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code",
                     refreshing ? "refresh_token" : "device_code", encoded);
    secret_free(encoded);
    return n < 0 ? NULL : form;
}

static esp_err_t handle_refresh_response(auth_client_t *auth, const cJSON *root, int status)
{
    classify_failure(auth, root, status);
    if (status == 200)
        return accept_tokens(auth, root, true);
    if (status == 400 && error_is(json_string(root, "error"), "invalid_grant")) {
        esp_err_t err = token_storage_write(NULL);
        if (err != ESP_OK) {
            auth->error = SERVICE_ERROR_STORAGE;
            return err;
        }
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGW(TAG, "Token endpoint returned HTTP %d; verify app registration if persistent", status);
    return ESP_FAIL;
}

/* Returns NOT_FOUND only when the user must authorize again. */
static esp_err_t refresh_access(auth_client_t *auth, unsigned *retry_after)
{
    char *refresh = NULL;
    esp_err_t err = token_storage_read(&refresh);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NOT_FOUND)
            auth->error = SERVICE_ERROR_STORAGE;
        return err;
    }
    char *form = token_form(refresh, true);
    secret_free(refresh);
    if (!form)
        return ESP_ERR_NO_MEM;
    http_response_t response;
    err = auth_request("token", form, &response);
    secret_free(form);
    *retry_after = response.retry_after;
    cJSON *root = err == ESP_OK ? response_json(&response.body) : NULL;
    if (err == ESP_OK)
        err = handle_refresh_response(auth, root, response.status);
    cJSON_Delete(root);
    http_response_free(&response);
    return err;
}

typedef struct {
    char *code;
    unsigned expires, interval;
} device_code_t;

static bool bounded_string(const char *value, size_t limit)
{
    return value && strlen(value) <= limit;
}

static esp_err_t accept_device_code(const cJSON *root, device_code_t *device)
{
    const char *code = json_string(root, "device_code");
    const char *message = json_string(root, "message");
    const char *user_code = json_string(root, "user_code");
    if (!message)
        return ESP_ERR_INVALID_RESPONSE;
    if (!bounded_string(code, TOKEN_LIMIT) || !bounded_string(user_code, 32))
        return ESP_ERR_INVALID_RESPONSE;
    if (!json_seconds(root, "expires_in", &device->expires))
        return ESP_ERR_INVALID_RESPONSE;
    if (cJSON_HasObjectItem(root, "interval") && !json_seconds(root, "interval", &device->interval))
        return ESP_ERR_INVALID_RESPONSE;
    device->code = strdup(code);
    if (!device->code)
        return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "%s", message);
    notify(AUTH_CODE_READY, user_code, esp_timer_get_time() + (int64_t)device->expires * 1000000);
    return ESP_OK;
}

static esp_err_t request_device_code(auth_client_t *auth, device_code_t *device,
                                     unsigned *retry_after)
{
    char *scope = form_encode(SCOPE);
    if (!scope)
        return ESP_ERR_NO_MEM;
    char *form = NULL;
    int n = asprintf(&form, "client_id=%s&scope=%s", settings_get()->client, scope);
    free(scope);
    if (n < 0)
        return ESP_ERR_NO_MEM;
    http_response_t response;
    esp_err_t err = auth_request("devicecode", form, &response);
    free(form);
    *retry_after = response.retry_after;
    cJSON *root = err == ESP_OK ? response_json(&response.body) : NULL;
    classify_failure(auth, root, response.status);
    if (err == ESP_OK)
        err = response.status == 200 ? accept_device_code(root, device) : ESP_ERR_INVALID_RESPONSE;
    cJSON_Delete(root);
    http_response_free(&response);
    return err;
}

typedef enum {
    TOKEN_POLL_SUCCESS,
    TOKEN_POLL_TRANSIENT,
    TOKEN_POLL_PENDING,
    TOKEN_POLL_SLOW_DOWN,
    TOKEN_POLL_TERMINAL,
} token_poll_result_t;

static token_poll_result_t classify_token_poll(esp_err_t err, int status, const char *error)
{
    if (err != ESP_OK)
        return TOKEN_POLL_TRANSIENT;
    if (status == 200)
        return TOKEN_POLL_SUCCESS;
    if (status == 429 || status >= 500)
        return TOKEN_POLL_TRANSIENT;
    if (error_is(error, "authorization_pending"))
        return TOKEN_POLL_PENDING;
    if (error_is(error, "slow_down"))
        return TOKEN_POLL_SLOW_DOWN;
    return TOKEN_POLL_TERMINAL;
}

static unsigned transient_interval(unsigned interval, unsigned retry_after)
{
    unsigned next = interval < 60 ? interval * 2 : interval;
    return retry_after > next ? retry_after : next;
}

static esp_err_t handle_token_poll(auth_client_t *auth, const cJSON *root,
                                   token_poll_result_t result, unsigned retry_after,
                                   unsigned *interval, bool *again)
{
    *again = false;
    switch (result) {
    case TOKEN_POLL_SUCCESS:
        return accept_tokens(auth, root, false);
    case TOKEN_POLL_TRANSIENT:
        *interval = transient_interval(*interval, retry_after);
        *again = true;
        return ESP_FAIL;
    case TOKEN_POLL_SLOW_DOWN:
        *interval += 5;
        /* fall through */
    case TOKEN_POLL_PENDING:
        *again = true;
        return ESP_ERR_NOT_FINISHED;
    default:
        /* Expired, declined, or invalid device code: finish this flow. */
        return ESP_ERR_INVALID_RESPONSE;
    }
}

static esp_err_t poll_for_token(auth_client_t *auth, const char *form, int64_t deadline,
                                unsigned interval, unsigned *retry_after)
{
    esp_err_t err = ESP_ERR_TIMEOUT;
    while (esp_timer_get_time() + (int64_t)interval * 1000000 < deadline) {
        task_wait_seconds(interval);
        wifi_wait_connected();
        if (esp_timer_get_time() >= deadline)
            break;
        http_response_t response;
        err = auth_request("token", form, &response);
        *retry_after = response.retry_after;
        cJSON *root = err == ESP_OK ? response_json(&response.body) : NULL;
        classify_failure(auth, root, response.status);
        token_poll_result_t result =
            classify_token_poll(err, response.status, json_string(root, "error"));
        bool again;
        err = handle_token_poll(auth, root, result, response.retry_after, &interval, &again);
        cJSON_Delete(root);
        http_response_free(&response);
        if (!again)
            break;
    }
    if (err == ESP_ERR_TIMEOUT || err == ESP_ERR_NOT_FINISHED)
        auth->error = SERVICE_ERROR_AUTH_EXPIRED;
    return err;
}

static esp_err_t device_login(auth_client_t *auth, unsigned *retry_after)
{
    device_code_t device = {.interval = 5};
    esp_err_t err = request_device_code(auth, &device, retry_after);
    if (err != ESP_OK)
        return err;
    int64_t deadline = esp_timer_get_time() + (int64_t)device.expires * 1000000;
    char *form = token_form(device.code, false);
    secret_free(device.code);
    if (!form)
        return ESP_ERR_NO_MEM;
    err = poll_for_token(auth, form, deadline, device.interval, retry_after);
    secret_free(form);
    return err;
}

bool auth_client_ready(const auth_client_t *auth)
{
    return auth && auth->access_token && esp_timer_get_time() < auth->access_deadline;
}

void auth_client_invalidate(auth_client_t *auth)
{
    if (!auth)
        return;
    secret_free(auth->access_token);
    auth->access_token = NULL;
    auth->access_deadline = 0;
}

static void finish_auth_attempt(auth_client_t *auth, esp_err_t err, unsigned *retry_after)
{
    if (err == ESP_OK)
        auth->error = SERVICE_ERROR_NONE;
    else if (auth->error == SERVICE_ERROR_AUTH_CONFIG && *retry_after < 300)
        *retry_after = 300;
    notify(err == ESP_OK ? AUTH_SIGNED_IN : AUTH_RETRYING, NULL, 0);
}

esp_err_t auth_client_ensure(auth_client_t *auth, unsigned *retry_after)
{
    if (!auth || !retry_after)
        return ESP_ERR_INVALID_ARG;
    *retry_after = 0;
    if (auth_client_ready(auth))
        return ESP_OK;
    auth->error = SERVICE_ERROR_AUTH;
    notify(AUTH_WAITING, NULL, 0);
    esp_err_t err = refresh_access(auth, retry_after);
    if (err == ESP_ERR_NOT_FOUND)
        err = device_login(auth, retry_after);
    finish_auth_attempt(auth, err, retry_after);
    return err;
}
