#include "graph_client.h"
#include "auth_client.h"
#include "diagnostics.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "http_transport.h"
#include "presence.h"
#include "sdkconfig.h"
#include "settings.h"
#include "task_time.h"
#include "wifi.h"
#include <stdio.h>
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

static void poll_presence_task(void *unused)
{
    if (!guid_valid(settings_get()->tenant) || !guid_valid(settings_get()->client) ||
        settings_get()->stale_seconds <= settings_get()->poll_seconds) {
        ESP_LOGE(TAG,
                 "Check tenant/client GUIDs and set stale timeout longer than polling interval");
        publish(SERVICE_CONFIG_ERROR);
        /* Keep ownership of worker until reboot; init must not create a second worker. */
        vTaskSuspend(NULL);
    }
    wifi_wait_connected();
    publish(SERVICE_SYNCING_CLOCK);
    esp_sntp_config_t time_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(settings_get()->ntp);
    esp_err_t err = esp_netif_sntp_init(&time_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot initialize time synchronization: %s", esp_err_to_name(err));
        status.error = SERVICE_ERROR_CLOCK;
        publish(SERVICE_ERROR);
        vTaskSuspend(NULL);
    }
    while (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000)) != ESP_OK) {
        status.error = SERVICE_ERROR_CLOCK;
        publish(SERVICE_ERROR);
        ESP_LOGW(TAG, "Waiting for clock synchronization before TLS");
    }
    auth_client_t auth = {0};
    unsigned backoff = 2;
    int64_t last_diagnostic = 0;
    for (;;) {
        diagnostics_sample("graph", &last_diagnostic);
        wifi_wait_connected();
        status.error = SERVICE_ERROR_NONE;
        int64_t poll_started = esp_timer_get_time();
        unsigned retry_after = 0;
        if (!auth_client_ready(&auth)) {
            publish(SERVICE_AUTHENTICATING);
            err = auth_client_ensure(&auth, &retry_after);
            status.error = auth.error;
            if (err == ESP_OK)
                publish(SERVICE_POLLING);
        } else
            err = ESP_OK;
        if (err == ESP_OK) {
            http_response_t response;
            err = http_request("https://graph.microsoft.com/v1.0/me/presence", NULL,
                               auth.access_token, &response);
            retry_after = response.retry_after;
            if (err == ESP_OK && response.status == 200) {
                cJSON *root = response_json(&response.body);
                if (!root || !json_string(root, "activity")) {
                    status.error = SERVICE_ERROR_RESPONSE;
                    err = ESP_ERR_INVALID_RESPONSE;
                } else {
                    presence_t presence = presence_from_json(root);
                    status.presence = presence;
                    snprintf(status.activity, sizeof(status.activity), "%s",
                             json_string(root, "activity"));
                    status.has_presence = true;
                    status.updated_at_us = esp_timer_get_time();
                    publish(SERVICE_READY);
                }
                cJSON_Delete(root);
            } else {
                status.error = err != ESP_OK ? SERVICE_ERROR_NETWORK : SERVICE_ERROR_RESPONSE;
                if (response.status == 429)
                    status.error = SERVICE_ERROR_THROTTLED;
                if (response.status == 401) {
                    status.error = SERVICE_ERROR_AUTH;
                    auth_client_invalidate(&auth);
                }
                if (response.status == 403) {
                    status.error = SERVICE_ERROR_PERMISSION;
                    ESP_LOGE(TAG,
                             "Presence permission denied; check app consent and tenant policy");
                    if (retry_after < 60)
                        retry_after = 60;
                }
                err = ESP_FAIL;
            }
            http_response_free(&response);
        }
        if (err == ESP_OK) {
            backoff = 2;
            task_wait_poll_slot(poll_started, settings_get()->poll_seconds);
        } else {
            publish(SERVICE_ERROR);
            ESP_LOGW(TAG, "Request failed: %s", esp_err_to_name(err));
            unsigned wait = retry_after > backoff ? retry_after : backoff;
            task_wait_seconds(wait);
            vTaskDelay(pdMS_TO_TICKS(esp_random() % 1000));
            if (backoff < 60)
                backoff = backoff > 30 ? 60 : backoff * 2;
        }
    }
}

esp_err_t graph_client_init(QueueHandle_t queue)
{
    if (!queue || worker)
        return ESP_ERR_INVALID_ARG;
    state_queue = queue;
    return xTaskCreate(poll_presence_task, "presence", 8192, NULL, 5, &worker) == pdPASS
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}
