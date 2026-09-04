#include "graph_client.h"
#include "auth_client.h"
#include "http_transport.h"
#include "presence.h"
#include "wifi.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include <string.h>

static const char *TAG = "graph";
static QueueHandle_t state_queue;
static TaskHandle_t worker;

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
    auth_client_t auth = {0};
    unsigned backoff = 2;
    for (;;) {
        wifi_wait_connected();
        unsigned retry_after = 0;
        if (!auth_client_ready(&auth)) {
            publish(STATE_TOKEN_REFRESH);
            err = auth_client_ensure(&auth, &retry_after);
            if (err == ESP_OK) publish(STATE_TOKEN_RECEIVED);
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
                    publish(presence == PRESENCE_UNKNOWN ? STATE_FAILED : (unsigned)presence);
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
