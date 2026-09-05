#include "auth_client.h"
#include "freertos/task.h"
#include "http_transport.h"
#include "nvs.h"
#include "presence.h"
#include "task_time.h"
#include "token_storage.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *stored;
static bool old_access;
static unsigned handles;
static esp_err_t storage_error, query_error, commit_error;
static bool wrong_type;
static size_t forced_size;
static int64_t now;
static unsigned sleeps[32], sleep_count;

typedef struct {
    const char *path;
    int status;
    const char *body;
    esp_err_t error;
    unsigned retry_after;
} exchange_t;
static const exchange_t *script;
static size_t script_count, request_count;

#define DEVICE                                                                                     \
    "{\"device_code\":\"a+b&c\",\"user_code\":\"ABCD-EFGH\",\"message\":\"Log "                    \
    "in\",\"interval\":5,\"expires_in\":900}"
#define TOKENS                                                                                     \
    "{\"access_token\":\"access\",\"refresh_token\":\"rotated\",\"token_type\":\"Bearer\","        \
    "\"expires_in\":3600}"
#define ACCESS_ONLY                                                                                \
    "{\"access_token\":\"new-access\",\"token_type\":\"Bearer\",\"expires_in\":3600}"

esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{
    (void)mode;
    assert(!strcmp(name, "graphapi"));
    if (storage_error)
        return storage_error;
    *handle = ++handles;
    return ESP_OK;
}
void nvs_close(nvs_handle_t handle)
{
    assert(handle && handles);
    --handles;
}
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *value, size_t *size)
{
    assert(handle && !strcmp(key, "refresh_token"));
    if (query_error)
        return query_error;
    if (wrong_type)
        return ESP_ERR_NVS_TYPE_MISMATCH;
    if (forced_size) {
        assert(!value);
        *size = forced_size;
        return ESP_OK;
    }
    if (!stored)
        return ESP_ERR_NVS_NOT_FOUND;
    size_t required = strlen(stored) + 1;
    if (value) {
        assert(*size >= required);
        memcpy(value, stored, required);
    }
    *size = required;
    return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value)
{
    assert(handle && !strcmp(key, "refresh_token"));
    secret_free(stored);
    stored = strdup(value);
    return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    assert(handle);
    if (!strcmp(key, "access_token")) {
        old_access = false;
        return ESP_OK;
    }
    assert(!strcmp(key, "refresh_token"));
    secret_free(stored);
    stored = NULL;
    forced_size = 0;
    wrong_type = false;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle);
    return commit_error;
}
int64_t esp_timer_get_time(void)
{
    return now;
}
void vTaskDelay(TickType_t ticks)
{
    assert(sleep_count < 32);
    sleeps[sleep_count++] = ticks / 1000;
    now += (int64_t)ticks * 1000;
}
void wifi_wait_connected(void) {}

esp_err_t http_request(const char *url, const char *form, const char *token,
                       http_response_t *response)
{
    assert(request_count < script_count);
    const exchange_t *step = &script[request_count++];
    assert(strstr(url, step->path) && form && !token);
    if (strstr(form, "grant_type=urn")) {
        assert(strstr(form, "urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code"));
        assert(strstr(form, "device_code=a%2Bb%26c"));
    }
    memset(response, 0, sizeof(*response));
    response->status = step->status;
    response->retry_after = step->retry_after;
    if (step->body) {
        size_t size = strlen(step->body) + 1;
        response->body = (response_buffer_t){calloc(size, 1), size, 0, false};
        assert(response_append(&response->body, step->body, size - 1));
    }
    return step->error;
}
void http_response_free(http_response_t *response)
{
    secret_free(response->body.data);
    memset(response, 0, sizeof(*response));
}

static void reset(const exchange_t *steps, size_t count)
{
    assert(!handles);
    secret_free(stored);
    stored = NULL;
    old_access = true;
    forced_size = 0;
    storage_error = 0;
    query_error = 0;
    commit_error = 0;
    wrong_type = false;
    now = 0;
    sleep_count = 0;
    request_count = 0;
    script = steps;
    script_count = count;
}

static auth_event_t last_event;
static unsigned code_events;
static void observe_auth(auth_event_t event, const char *code, int64_t deadline)
{
    last_event = event;
    if (event == AUTH_CODE_READY) {
        assert(code && !strcmp(code, "ABCD-EFGH"));
        assert(deadline > now);
        ++code_events;
    } else {
        assert(!code && !deadline);
    }
}

static void device_and_refresh(void)
{
    const exchange_t steps[] = {
        {"/devicecode", 200, DEVICE, ESP_OK, 0},
        {"/token", 400, "{\"error\":\"authorization_pending\"}", ESP_OK, 0},
        {"/token", 400, "{\"error\":\"slow_down\"}", ESP_OK, 0},
        {"/token", 200, TOKENS, ESP_OK, 0},
        {"/token", 200, ACCESS_ONLY, ESP_OK, 0},
    };
    reset(steps, 5);
    auth_client_t auth = {0};
    unsigned retry;
    assert(auth_client_ensure(&auth, &retry) == ESP_OK);
    assert(request_count == 4 && sleep_count == 3);
    assert(sleeps[0] == 5 && sleeps[1] == 5 && sleeps[2] == 10);
    assert(!old_access && !strcmp(stored, "rotated"));
    assert(auth_client_ready(&auth));
    assert(auth_client_ensure(&auth, &retry) == ESP_OK && request_count == 4);
    now = auth.access_deadline;
    assert(!auth_client_ready(&auth));
    assert(auth_client_ensure(&auth, &retry) == ESP_OK && request_count == 5);
    assert(!strcmp(auth.access_token, "new-access") && !strcmp(stored, "rotated"));
    auth_client_invalidate(&auth);
}

static void revocation_and_transient(void)
{
    const exchange_t revoked[] = {
        {"/token", 400, "{\"error\":\"invalid_grant\"}", ESP_OK, 0},
        {"/devicecode", 200, DEVICE, ESP_OK, 0},
        {"/token", 200, TOKENS, ESP_OK, 0},
    };
    reset(revoked, 3);
    stored = strdup("revoked");
    auth_client_t auth = {0};
    unsigned retry;
    assert(auth_client_ensure(&auth, &retry) == ESP_OK && request_count == 3);
    auth_client_invalidate(&auth);
    const exchange_t truncated[] = {{"/token", 200, "{", ESP_ERR_INVALID_SIZE, 0}};
    reset(truncated, 1);
    stored = strdup("keep-me");
    assert(auth_client_ensure(&auth, &retry) == ESP_ERR_INVALID_SIZE);
    assert(request_count == 1 && !strcmp(stored, "keep-me") && !auth.access_token);
    const exchange_t throttled[] = {{"/token", 429, "{}", ESP_OK, 120}};
    reset(throttled, 1);
    stored = strdup("keep-me");
    assert(auth_client_ensure(&auth, &retry) != ESP_OK && retry == 120);
    assert(!strcmp(stored, "keep-me"));
}

static void invalid_and_missing_storage(void)
{
    reset(NULL, 0);
    char *token = (char *)1;
    assert(token_storage_read(&token) == ESP_ERR_NOT_FOUND && !token && !handles);
    forced_size = TOKEN_LIMIT + 2;
    assert(token_storage_read(&token) == ESP_ERR_NOT_FOUND && !forced_size && !handles);
    wrong_type = true;
    assert(token_storage_read(&token) == ESP_ERR_NOT_FOUND && !wrong_type && !handles);
    assert(token_storage_read(NULL) == ESP_ERR_INVALID_ARG);
    assert(token_storage_write("") == ESP_ERR_INVALID_ARG);
    query_error = ESP_FAIL;
    assert(token_storage_read(&token) == ESP_FAIL && !token && !handles);
    query_error = 0;
    forced_size = TOKEN_LIMIT + 2;
    commit_error = ESP_FAIL;
    assert(token_storage_read(&token) == ESP_FAIL && !token && !handles);
    commit_error = 0;
    storage_error = ESP_FAIL;
    assert(token_storage_read(&token) == ESP_FAIL && !token && !handles);
}

static void terminal_auth_errors(void)
{
    const char *errors[] = {"authorization_declined", "expired_token", "bad_verification_code"};
    for (unsigned i = 0; i < 3; ++i) {
        char body[96];
        snprintf(body, sizeof(body), "{\"error\":\"%s\"}", errors[i]);
        const exchange_t steps[] = {{"/devicecode", 200, DEVICE, ESP_OK, 0},
                                    {"/token", 400, body, ESP_OK, 0}};
        reset(steps, 2);
        auth_client_t auth = {0};
        unsigned retry;
        assert(auth_client_ensure(&auth, &retry) == ESP_ERR_INVALID_RESPONSE);
        assert(request_count == 2 && !auth.access_token);
    }
}

static void rejected_tokens_and_deadlines(void)
{
    const char *invalid[] = {
        "{}", "{\"access_token\":true,\"token_type\":\"Bearer\",\"expires_in\":3600}",
        "{\"access_token\":\"bad\\r\\nheader\",\"token_type\":\"Bearer\",\"expires_in\":3600}",
        "{\"access_token\":\"access\",\"token_type\":\"Bearer\",\"expires_in\":0}"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        const exchange_t steps[] = {{"/token", 200, invalid[i], ESP_OK, 0}};
        reset(steps, 1);
        stored = strdup("keep");
        auth_client_t auth = {0};
        unsigned retry;
        assert(auth_client_ensure(&auth, &retry) == ESP_ERR_INVALID_RESPONSE);
        assert(!auth.access_token && !strcmp(stored, "keep") && request_count == 1);
    }
    const exchange_t expired[] = {{"/devicecode", 200,
                                   "{\"device_code\":\"a+b&c\",\"user_code\":\"ABCD-EFGH\","
                                   "\"message\":\"Login\",\"interval\":5,\"expires_in\":5}",
                                   ESP_OK, 0}};
    reset(expired, 1);
    auth_client_t auth = {0};
    unsigned retry;
    assert(auth_client_ensure(&auth, &retry) == ESP_ERR_TIMEOUT && request_count == 1);
    const exchange_t throttled[] = {{"/devicecode", 200, DEVICE, ESP_OK, 0},
                                    {"/token", 429, "{}", ESP_OK, 1200}};
    reset(throttled, 2);
    assert(auth_client_ensure(&auth, &retry) == ESP_FAIL && retry == 1200 && request_count == 2);
    const exchange_t write_failed[] = {{"/token", 200, TOKENS, ESP_OK, 0}};
    reset(write_failed, 1);
    stored = strdup("keep");
    commit_error = ESP_FAIL;
    assert(auth_client_ensure(&auth, &retry) == ESP_FAIL && !auth.access_token && !handles);
}

static void presence_mapping(void)
{
    const struct {
        const char *activity;
        presence_t expected;
    } cases[] = {
        {"Available", PRESENCE_AVAILABLE},       {"Busy", PRESENCE_BUSY},
        {"InAMeeting", PRESENCE_DO_NOT_DISTURB}, {"Presenting", PRESENCE_DO_NOT_DISTURB},
        {"OffWork", PRESENCE_OFF_WORK},          {"PresenceUnknown", PRESENCE_UNKNOWN},
        {"FutureActivity", PRESENCE_UNKNOWN},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "activity", cases[i].activity);
        assert(presence_from_json(root) == cases[i].expected);
        cJSON_Delete(root);
    }
    assert(presence_from_json(NULL) == PRESENCE_UNKNOWN);
    cJSON *root = cJSON_Parse("{\"activity\":3}");
    assert(presence_from_json(root) == PRESENCE_UNKNOWN);
    cJSON_Delete(root);
}

int main(void)
{
    auth_client_observe(observe_auth);
    device_and_refresh();
    assert(code_events == 1 && last_event == AUTH_SIGNED_IN);
    revocation_and_transient();
    invalid_and_missing_storage();
    terminal_auth_errors();
    rejected_tokens_and_deadlines();
    assert(last_event == AUTH_RETRYING);
    presence_mapping();
    reset(NULL, 0);
    task_wait_seconds(121);
    assert(sleep_count == 3 && sleeps[0] == 60 && sleeps[1] == 60 && sleeps[2] == 1);
    assert(now == 121000000);
    puts("authentication, storage, and presence tests passed");
}
