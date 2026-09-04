#include "graph_client.h"
#include "auth_client.h"
#include "http_transport.h"
#include "presence.h"
#include "wifi.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "graph";
static QueueHandle_t state_queue;
static TaskHandle_t worker;

static app_status_t status = {.service = SERVICE_CONNECTING, .presence = PRESENCE_UNKNOWN};

static void publish(service_state_t service)
{
    status.service = service;
    xQueueOverwrite(state_queue, &status);
}

static void delay_seconds(unsigned seconds)
{
    vTaskDelay(pdMS_TO_TICKS(seconds * 1000U));
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
    if (!guid_valid(CONFIG_AAD_TENANT_ID) || !guid_valid(CONFIG_AAD_CLIENT_ID) ||
        CONFIG_PRESENCE_STALE_SECONDS <= CONFIG_PRESENCE_POLL_SECONDS) {
        ESP_LOGE(TAG, "Check tenant/client GUIDs and set stale timeout longer than polling interval");
        publish(SERVICE_CONFIG_ERROR);
        /* Keep ownership of worker until reboot; init must not create a second worker. */
        vTaskSuspend(NULL);
    }
    wifi_wait_connected();
    publish(SERVICE_SYNCING_CLOCK);
    esp_sntp_config_t time_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t err = esp_netif_sntp_init(&time_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot initialize time synchronization: %s", esp_err_to_name(err));
        publish(SERVICE_ERROR);
        vTaskSuspend(NULL);
    }
    while (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000)) != ESP_OK) {
        publish(SERVICE_ERROR);
        ESP_LOGW(TAG, "Waiting for clock synchronization before TLS");
    }
    auth_client_t auth = {0};
    unsigned backoff = 2;
    for (;;) {
        wifi_wait_connected();
        unsigned retry_after = 0;
        if (!auth_client_ready(&auth)) {
            publish(SERVICE_AUTHENTICATING);
            err = auth_client_ensure(&auth, &retry_after);
            if (err == ESP_OK) publish(SERVICE_POLLING);
        } else err = ESP_OK;
        if (err == ESP_OK) {
            http_response_t response;
            err = http_request("https://graph.microsoft.com/beta/me/presence", NULL, auth.access_token, &response);
            retry_after = response.retry_after;
            if (err == ESP_OK && response.status == 200) {
                cJSON *root = response_json(&response.body);
                if (!root || !json_string(root, "activity")) err = ESP_ERR_INVALID_RESPONSE;
                else {
                    presence_t presence = presence_from_json(root);
                    status.presence = presence;
                    status.has_presence = true;
                    status.updated_at_us = esp_timer_get_time();
                    publish(SERVICE_READY);
                }
                cJSON_Delete(root);
            } else {
                if (response.status == 401) { auth_client_invalidate(&auth); }
                if (response.status == 403) {
                    ESP_LOGE(TAG, "Presence permission denied; check app consent and tenant policy");
                    retry_after = 60;
                }
                err = ESP_FAIL;
            }
            http_response_free(&response);
        }
        if (err == ESP_OK) { backoff = 2; delay_seconds(CONFIG_PRESENCE_POLL_SECONDS); }
        else {
            publish(SERVICE_ERROR);
            ESP_LOGW(TAG, "Request failed: %s", esp_err_to_name(err));
            unsigned wait = retry_after > backoff ? retry_after : backoff;
            delay_seconds(wait);
            vTaskDelay(pdMS_TO_TICKS(esp_random() % 1000));
            if (backoff < 60) backoff = backoff > 30 ? 60 : backoff * 2;
        }
    }
}

esp_err_t graph_client_init(QueueHandle_t queue)
{
    if (!queue || worker) return ESP_ERR_INVALID_ARG;
    state_queue = queue;
    return xTaskCreate(poll_presence_task, "presence", 8192, NULL, 5, &worker) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}
