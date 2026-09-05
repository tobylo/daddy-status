#include "auth_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_transport.h"
#include "sdkconfig.h"
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

static esp_err_t accept_tokens(auth_client_t *auth, const cJSON *root, bool refreshing)
{
    const char *access = json_string(root, "access_token");
    const char *refresh = json_string(root, "refresh_token");
    const char *type = json_string(root, "token_type");
    unsigned expires;
    if (!bearer_token_valid(access) || !type || strcasecmp(type, "Bearer") ||
        (!refreshing && !refresh) || (refresh && strlen(refresh) > TOKEN_LIMIT) ||
        !json_seconds(root, "expires_in", &expires))
        return ESP_ERR_INVALID_RESPONSE;
    char *copy = strdup(access);
    if (!copy)
        return ESP_ERR_NO_MEM;
    esp_err_t err = refresh ? token_storage_write(refresh) : ESP_OK;
    if (err != ESP_OK) {
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
                     CONFIG_AAD_TENANT_ID, path);
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
    int n = asprintf(&form, "client_id=%s&grant_type=%s&%s=%s", CONFIG_AAD_CLIENT_ID,
                     refreshing ? "refresh_token"
                                : "urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code",
                     refreshing ? "refresh_token" : "device_code", encoded);
    secret_free(encoded);
    return n < 0 ? NULL : form;
}

/* Returns NOT_FOUND only when the user must authorize again. */
static esp_err_t refresh_access(auth_client_t *auth, unsigned *retry_after)
{
    char *refresh = NULL;
    esp_err_t err = token_storage_read(&refresh);
    if (err != ESP_OK)
        return err;
    char *form = token_form(refresh, true);
    secret_free(refresh);
    if (!form)
        return ESP_ERR_NO_MEM;
    http_response_t response;
    err = auth_request("token", form, &response);
    secret_free(form);
    *retry_after = response.retry_after;
    cJSON *root = err == ESP_OK ? response_json(&response.body) : NULL;
    if (err == ESP_OK) {
        const char *code = json_string(root, "error");
        if (response.status == 200)
            err = accept_tokens(auth, root, true);
        else if (response.status == 400 && code && !strcmp(code, "invalid_grant")) {
            err = token_storage_write(NULL);
            if (err == ESP_OK)
                err = ESP_ERR_NOT_FOUND;
        } else {
            ESP_LOGW(TAG, "Token endpoint returned HTTP %d; verify app registration if persistent",
                     response.status);
            err = ESP_FAIL;
        }
    }
    cJSON_Delete(root);
    http_response_free(&response);
    return err;
}

static esp_err_t device_login(auth_client_t *auth, unsigned *retry_after)
{
    char *scope = form_encode(SCOPE);
    if (!scope)
        return ESP_ERR_NO_MEM;
    char *form = NULL;
    int n = asprintf(&form, "client_id=%s&scope=%s", CONFIG_AAD_CLIENT_ID, scope);
    free(scope);
    if (n < 0)
        return ESP_ERR_NO_MEM;
    http_response_t response;
    esp_err_t err = auth_request("devicecode", form, &response);
    free(form);
    *retry_after = response.retry_after;
    cJSON *root = err == ESP_OK ? response_json(&response.body) : NULL;
    const char *code = json_string(root, "device_code");
    const char *message = json_string(root, "message");
    const char *user_code = json_string(root, "user_code");
    unsigned expires = 0, interval = 5;
    char *device = NULL;
    if (err == ESP_OK && response.status == 200 && code && strlen(code) <= TOKEN_LIMIT && message &&
        user_code && strlen(user_code) <= 32 && json_seconds(root, "expires_in", &expires)) {
        if (cJSON_HasObjectItem(root, "interval") && !json_seconds(root, "interval", &interval)) {
            err = ESP_ERR_INVALID_RESPONSE;
        } else {
            device = strdup(code);
            if (!device)
                err = ESP_ERR_NO_MEM;
            else {
                ESP_LOGI(TAG, "%s", message);
                notify(AUTH_CODE_READY, user_code,
                       esp_timer_get_time() + (int64_t)expires * 1000000);
            }
        }
    } else if (err == ESP_OK)
        err = ESP_ERR_INVALID_RESPONSE;
    cJSON_Delete(root);
    http_response_free(&response);
    if (err != ESP_OK)
        return err;
    int64_t deadline = esp_timer_get_time() + (int64_t)expires * 1000000;
    form = token_form(device, false);
    secret_free(device);
    if (!form)
        return ESP_ERR_NO_MEM;
    err = ESP_ERR_TIMEOUT;
    while (esp_timer_get_time() + (int64_t)interval * 1000000 < deadline) {
        task_wait_seconds(interval);
        wifi_wait_connected();
        if (esp_timer_get_time() >= deadline)
            break;
        err = auth_request("token", form, &response);
        *retry_after = response.retry_after;
        root = err == ESP_OK ? response_json(&response.body) : NULL;
        const char *error = json_string(root, "error");
        bool again = false;
        if (err == ESP_OK && response.status == 200) {
            err = accept_tokens(auth, root, false);
        } else if (err != ESP_OK || response.status == 429 || response.status >= 500) {
            interval = interval < 60 ? interval * 2 : interval;
            if (response.retry_after > interval)
                interval = response.retry_after;
            err = ESP_FAIL;
            again = true;
        } else if (error &&
                   (!strcmp(error, "authorization_pending") || !strcmp(error, "slow_down"))) {
            if (!strcmp(error, "slow_down"))
                interval += 5;
            err = ESP_ERR_NOT_FINISHED;
            again = true;
        } else {
            /* Expired, declined, or invalid device code: finish this flow. */
            err = ESP_ERR_INVALID_RESPONSE;
        }
        cJSON_Delete(root);
        http_response_free(&response);
        if (!again)
            break;
    }
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

esp_err_t auth_client_ensure(auth_client_t *auth, unsigned *retry_after)
{
    if (!auth || !retry_after)
        return ESP_ERR_INVALID_ARG;
    *retry_after = 0;
    if (auth_client_ready(auth))
        return ESP_OK;
    notify(AUTH_WAITING, NULL, 0);
    esp_err_t err = refresh_access(auth, retry_after);
    if (err == ESP_ERR_NOT_FOUND)
        err = device_login(auth, retry_after);
    notify(err == ESP_OK ? AUTH_SIGNED_IN : AUTH_RETRYING, NULL, 0);
    return err;
}
