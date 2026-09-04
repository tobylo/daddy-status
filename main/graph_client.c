#include "graph_client.h"
#include "protocol.h"
#include "wifi.h"
#include "freertos/task.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define RESPONSE_CAPACITY (16384 + 1)
#define TOKEN_LIMIT 8192
static const char *TAG = "graph";
static const char *SCOPE = "https://graph.microsoft.com/Presence.Read offline_access";
static QueueHandle_t state_queue;
static TaskHandle_t worker;
static char *access_token;
static int64_t access_deadline;

typedef struct {
    response_buffer_t body;
    int status;
    unsigned retry_after;
} http_response_t;

static void publish(unsigned state)
{
    if (xQueueSend(state_queue, &state, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Status queue full");
    }
}

static void delay_seconds(unsigned seconds)
{
    vTaskDelay(pdMS_TO_TICKS(seconds * 1000U));
}

static esp_err_t http_event(esp_http_client_event_t *event)
{
    http_response_t *response = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        if (!response_append(&response->body, event->data, (size_t)event->data_len)) {
            return ESP_ERR_INVALID_SIZE;
        }
    } else if (event->event_id == HTTP_EVENT_ON_HEADER &&
               strcasecmp(event->header_key, "Retry-After") == 0) {
        response->retry_after = retry_after_seconds(event->header_value);
    }
    return ESP_OK;
}

static void response_free(http_response_t *response)
{
    secret_free(response->body.data);
    memset(response, 0, sizeof(*response));
}

static esp_err_t request(const char *url, const char *form, const char *token,
                         http_response_t *response)
{
    memset(response, 0, sizeof(*response));
    response->body.data = calloc(RESPONSE_CAPACITY, 1);
    if (!response->body.data) return ESP_ERR_NO_MEM;
    response->body.capacity = RESPONSE_CAPACITY;
    const esp_http_client_config_t config = {
        .url = url,
        .method = form ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .event_handler = http_event,
        .user_data = response,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .disable_auto_redirect = true,
        .buffer_size = 2048,
        .buffer_size_tx = TOKEN_LIMIT + 128,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_http_client_set_header(client, "Accept", "application/json");
    char *authorization = NULL;
    if (err == ESP_OK && token) {
        if (asprintf(&authorization, "Bearer %s", token) < 0) err = ESP_ERR_NO_MEM;
        else err = esp_http_client_set_header(client, "Authorization", authorization);
    }
    if (err == ESP_OK && form) {
        err = esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
        if (err == ESP_OK) err = esp_http_client_set_post_field(client, form, strlen(form));
    }
    if (err == ESP_OK) err = esp_http_client_perform(client);
    response->status = esp_http_client_get_status_code(client);
    if (err == ESP_OK && (response->body.overflow || !esp_http_client_is_complete_data_received(client))) {
        err = ESP_ERR_INVALID_SIZE;
    }
    esp_http_client_cleanup(client);
    secret_free(authorization);
    return err;
}

static esp_err_t token_read(char **token)
{
    *token = NULL;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("graphapi", NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    size_t size = 0;
    err = nvs_get_str(handle, "refresh_token", NULL, &size);
    if (err == ESP_OK && (size < 2 || size > TOKEN_LIMIT + 1)) err = ESP_ERR_INVALID_SIZE;
    if (err == ESP_OK) {
        *token = calloc(size, 1);
        if (!*token) err = ESP_ERR_NO_MEM;
        else {
            (*token)[0] = '\0';
            err = nvs_get_str(handle, "refresh_token", *token, &size);
        }
    }
    nvs_close(handle);
    if (err != ESP_OK) { secret_free(*token); *token = NULL; }
    return err;
}

static esp_err_t token_store(const char *token)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("graphapi", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = token ? nvs_set_str(handle, "refresh_token", token) : nvs_erase_key(handle, "refresh_token");
    if (err == ESP_ERR_NVS_NOT_FOUND && !token) err = ESP_OK;
    if (err == ESP_OK) {
        esp_err_t erased = nvs_erase_key(handle, "access_token");
        if (erased != ESP_OK && erased != ESP_ERR_NVS_NOT_FOUND) err = erased;
    }
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static esp_err_t accept_tokens(const cJSON *root, bool refreshing)
{
    const char *access = json_string(root, "access_token");
    const char *refresh = json_string(root, "refresh_token");
    const char *type = json_string(root, "token_type");
    unsigned expires;
    if (!access || strlen(access) > TOKEN_LIMIT || !type || strcasecmp(type, "Bearer") ||
        (!refreshing && !refresh) || (refresh && strlen(refresh) > TOKEN_LIMIT) ||
        !json_seconds(root, "expires_in", &expires)) return ESP_ERR_INVALID_RESPONSE;
    char *copy = strdup(access);
    if (!copy) return ESP_ERR_NO_MEM;
    esp_err_t err = refresh ? token_store(refresh) : ESP_OK;
    if (err != ESP_OK) { secret_free(copy); return err; }
    secret_free(access_token);
    access_token = copy;
    unsigned margin = expires > 120 ? 60 : expires / 2;
    access_deadline = esp_timer_get_time() + (int64_t)(expires - margin) * 1000000;
    return ESP_OK;
}

static esp_err_t auth_request(const char *path, const char *form, http_response_t *response)
{
    char url[160];
    int n = snprintf(url, sizeof(url), "https://login.microsoftonline.com/%s/oauth2/v2.0/%s",
                     CONFIG_AAD_TENANT_ID, path);
    if (n < 0 || n >= sizeof(url)) { memset(response, 0, sizeof(*response)); return ESP_ERR_INVALID_SIZE; }
    return request(url, form, NULL, response);
}

static char *token_form(const char *value, bool refreshing)
{
    char *encoded = form_encode(value);
    if (!encoded) return NULL;
    char *form = NULL;
    int n = asprintf(&form, "client_id=%s&grant_type=%s&%s=%s", CONFIG_AAD_CLIENT_ID,
                     refreshing ? "refresh_token" : "urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code",
                     refreshing ? "refresh_token" : "device_code", encoded);
    secret_free(encoded);
    return n < 0 ? NULL : form;
}

/* Returns NOT_FOUND only when the user must authorize again. */
static esp_err_t refresh_access(unsigned *retry_after)
{
    char *refresh = NULL;
    esp_err_t err = token_read(&refresh);
    if (err != ESP_OK) return err;
    char *form = token_form(refresh, true);
    secret_free(refresh);
    if (!form) return ESP_ERR_NO_MEM;
    http_response_t response;
    err = auth_request("token", form, &response);
    secret_free(form);
    *retry_after = response.retry_after;
    cJSON *root = err == ESP_OK ? response_json(&response.body) : NULL;
    if (err == ESP_OK) {
        const char *code = json_string(root, "error");
        if (response.status == 200) err = accept_tokens(root, true);
        else if (response.status == 400 && code && !strcmp(code, "invalid_grant")) {
            err = token_store(NULL);
            if (err == ESP_OK) err = ESP_ERR_NVS_NOT_FOUND;
        } else {
            ESP_LOGW(TAG, "Token endpoint returned HTTP %d; verify app registration if persistent", response.status);
            err = ESP_FAIL;
        }
    }
    cJSON_Delete(root);
    response_free(&response);
    return err;
}

static esp_err_t device_login(unsigned *retry_after)
{
    char *scope = form_encode(SCOPE);
    if (!scope) return ESP_ERR_NO_MEM;
    char *form = NULL;
    int n = asprintf(&form, "client_id=%s&scope=%s", CONFIG_AAD_CLIENT_ID, scope);
    free(scope);
    if (n < 0) return ESP_ERR_NO_MEM;
    http_response_t response;
    esp_err_t err = auth_request("devicecode", form, &response);
    free(form);
    *retry_after = response.retry_after;
    cJSON *root = err == ESP_OK ? response_json(&response.body) : NULL;
    const char *code = json_string(root, "device_code");
    const char *message = json_string(root, "message");
    unsigned expires = 0, interval = 5;
    char *device = NULL;
    if (err == ESP_OK && response.status == 200 && code && strlen(code) <= TOKEN_LIMIT && message &&
        json_seconds(root, "expires_in", &expires)) {
        if (cJSON_HasObjectItem(root, "interval") && !json_seconds(root, "interval", &interval)) {
            err = ESP_ERR_INVALID_RESPONSE;
        } else {
            device = strdup(code);
            if (!device) err = ESP_ERR_NO_MEM;
            else ESP_LOGI(TAG, "%s", message);
        }
    } else if (err == ESP_OK) err = ESP_ERR_INVALID_RESPONSE;
    cJSON_Delete(root);
    response_free(&response);
    if (err != ESP_OK) return err;
    int64_t deadline = esp_timer_get_time() + (int64_t)expires * 1000000;
    form = token_form(device, false);
    secret_free(device);
    if (!form) return ESP_ERR_NO_MEM;
    err = ESP_ERR_TIMEOUT;
    while (esp_timer_get_time() + (int64_t)interval * 1000000 < deadline) {
        delay_seconds(interval);
        wifi_wait_connected();
        if (esp_timer_get_time() >= deadline) break;
        err = auth_request("token", form, &response);
        root = err == ESP_OK ? response_json(&response.body) : NULL;
        const char *error = json_string(root, "error");
        bool again = false;
        if (err == ESP_OK && response.status == 200) {
            err = accept_tokens(root, false);
        } else if (err != ESP_OK || response.status == 429 || response.status >= 500) {
            interval = interval < 60 ? interval * 2 : interval;
            if (response.retry_after > interval) interval = response.retry_after;
            err = ESP_FAIL;
            again = true;
        } else if (error && (!strcmp(error, "authorization_pending") || !strcmp(error, "slow_down"))) {
            if (!strcmp(error, "slow_down")) interval += 5;
            err = ESP_ERR_NOT_FINISHED;
            again = true;
        } else {
            /* Expired, declined, or invalid device code: finish this flow. */
            err = ESP_ERR_INVALID_RESPONSE;
        }
        cJSON_Delete(root);
        response_free(&response);
        if (!again) break;
    }
    secret_free(form);
    return err;
}

static unsigned presence_state(const cJSON *root)
{
    const char *activity = json_string(root, "activity");
    if (!activity) return STATE_FAILED;
    static const struct { const char *activity; unsigned state; } states[] = {
        {"Available", PRESENCE_AVAILABLE}, {"AvailableIdle", PRESENCE_AVAILABLE},
        {"Away", PRESENCE_AVAILABLE}, {"BeRightBack", PRESENCE_AVAILABLE},
        {"Inactive", PRESENCE_AVAILABLE}, {"Busy", PRESENCE_BUSY}, {"BusyIdle", PRESENCE_BUSY},
        {"DoNotDisturb", PRESENCE_DO_NOT_DISTURB}, {"InACall", PRESENCE_DO_NOT_DISTURB},
        {"InAConferenceCall", PRESENCE_DO_NOT_DISTURB}, {"InAMeeting", PRESENCE_DO_NOT_DISTURB},
        {"Presenting", PRESENCE_DO_NOT_DISTURB}, {"UrgentInterruptionsOnly", PRESENCE_DO_NOT_DISTURB},
        {"Offline", PRESENCE_OFF_WORK}, {"OffWork", PRESENCE_OFF_WORK}, {"OutOfOffice", PRESENCE_OFF_WORK},
    };
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
        if (!strcmp(activity, states[i].activity)) return states[i].state;
    }
    return STATE_FAILED;
}

static bool guid_valid(const char *value)
{
    if (strlen(value) != 36) return false;
    for (size_t i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (value[i] != '-') return false;
        } else if (!((value[i] >= '0' && value[i] <= '9') ||
                     (value[i] >= 'a' && value[i] <= 'f') ||
                     (value[i] >= 'A' && value[i] <= 'F'))) return false;
    }
    return true;
}

static void poll_presence_task(void *unused)
{
    if (!guid_valid(CONFIG_AAD_TENANT_ID) || !guid_valid(CONFIG_AAD_CLIENT_ID)) {
        ESP_LOGE(TAG, "Set tenant and client GUIDs in menuconfig before connecting");
        publish(STATE_FAILED);
        /* Keep ownership of worker until reboot; init must not create a second worker. */
        vTaskSuspend(NULL);
    }
    wifi_wait_connected();
    esp_sntp_config_t time_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t err = esp_netif_sntp_init(&time_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot initialize time synchronization: %s", esp_err_to_name(err));
        publish(STATE_FAILED);
        vTaskSuspend(NULL);
    }
    while (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000)) != ESP_OK) {
        publish(STATE_FAILED);
        ESP_LOGW(TAG, "Waiting for clock synchronization before TLS");
    }
    unsigned backoff = 2;
    for (;;) {
        wifi_wait_connected();
        unsigned retry_after = 0;
        if (!access_token || esp_timer_get_time() >= access_deadline) {
            publish(STATE_TOKEN_REFRESH);
            err = refresh_access(&retry_after);
            if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_INVALID_SIZE) err = device_login(&retry_after);
            if (err == ESP_OK) publish(STATE_TOKEN_RECEIVED);
        } else err = ESP_OK;
        if (err == ESP_OK) {
            http_response_t response;
            err = request("https://graph.microsoft.com/beta/me/presence", NULL, access_token, &response);
            retry_after = response.retry_after;
            if (err == ESP_OK && response.status == 200) {
                cJSON *root = response_json(&response.body);
                if (!root || !json_string(root, "activity")) err = ESP_ERR_INVALID_RESPONSE;
                else publish(presence_state(root));
                cJSON_Delete(root);
            } else {
                if (response.status == 401) { secret_free(access_token); access_token = NULL; }
                if (response.status == 403) {
                    ESP_LOGE(TAG, "Presence permission denied; check app consent and tenant policy");
                    retry_after = 60;
                }
                err = ESP_FAIL;
            }
            response_free(&response);
        }
        if (err == ESP_OK) { backoff = 2; delay_seconds(2); }
        else {
            publish(STATE_FAILED);
            ESP_LOGW(TAG, "Request failed: %s", esp_err_to_name(err));
            unsigned wait = retry_after > backoff ? retry_after : backoff;
            delay_seconds(wait);
            vTaskDelay(pdMS_TO_TICKS(esp_random() % 1000));
            if (backoff < 60) backoff = backoff > 30 ? 60 : backoff * 2;
        }
    }
}

esp_err_t graph_client_init(QueueHandle_t *queue)
{
    if (!queue || !*queue || worker) return ESP_ERR_INVALID_ARG;
    state_queue = *queue;
    return xTaskCreate(poll_presence_task, "presence", 8192, NULL, 5, &worker) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}
