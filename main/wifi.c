#include "wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

static EventGroupHandle_t wifi_events;
static TaskHandle_t reconnect_task;
#define CONNECTED_BIT BIT0

static void event_handler(void *ctx, esp_event_base_t base, int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = data;
        ESP_LOGI("wifi", "Open http://" IPSTR "/ to sign in", IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_events, CONNECTED_BIT);
    } else if (base == WIFI_EVENT &&
               (id == WIFI_EVENT_STA_START || id == WIFI_EVENT_STA_DISCONNECTED)) {
        xEventGroupClearBits(wifi_events, CONNECTED_BIT);
    }
    if (reconnect_task)
        xTaskNotifyGive(reconnect_task);
}

static void reconnect(void *unused)
{
    unsigned backoff = 1;
    for (;;) {
        if (wifi_is_connected()) {
            backoff = 1;
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK)
            ESP_LOGW("wifi", "Connect attempt failed: %s", esp_err_to_name(err));
        /* Notifications update connection state; an early failure must not bypass backoff. */
        vTaskDelay(pdMS_TO_TICKS(backoff * 1000));
        if (backoff < 30)
            backoff = backoff > 15 ? 30 : backoff * 2;
    }
}

void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(wifi_events ? ESP_OK : ESP_ERR_NO_MEM);
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(netif ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_netif_set_hostname(netif, "daddy-status"));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t config = {.sta = {
                                .ssid = CONFIG_WIFI_SSID,
                                .password = CONFIG_WIFI_PASSWORD,
                                .listen_interval = CONFIG_WIFI_LISTEN_INTERVAL,
                            }};
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(xTaskCreate(reconnect, "wifi_reconnect", 3072, NULL, 4, &reconnect_task) ==
                            pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
#if CONFIG_POWER_SAVE_MAX_MODEM
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));
#elif CONFIG_POWER_SAVE_MIN_MODEM
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
#else
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
#endif
}

void wifi_wait_connected(void)
{
    xEventGroupWaitBits(wifi_events, CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
}

bool wifi_is_connected(void)
{
    return wifi_events && (xEventGroupGetBits(wifi_events) & CONNECTED_BIT);
}
