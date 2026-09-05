#include "diagnostics.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "graph_client.h"
#include "ledcontrol.h"
#include "nvs_flash.h"
#include "protocol.h"
#include "sdkconfig.h"
#include "task_time.h"
#include "web_server.h"
#include "wifi.h"

static void nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW("main", "NVS requires reinitialization; saved authorization will be erased");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    ESP_ERROR_CHECK(leds_init());
    if (!CONFIG_WIFI_SSID[0] || !guid_valid(CONFIG_AAD_TENANT_ID) ||
        !guid_valid(CONFIG_AAD_CLIENT_ID) || !CONFIG_NTP_SERVER[0] ||
        CONFIG_PRESENCE_STALE_SECONDS <= CONFIG_PRESENCE_POLL_SECONDS) {
        ESP_LOGE(
            "main",
            "Configure Wi-Fi, tenant/client GUIDs, NTP, and polling/stale intervals in menuconfig");
        ESP_ERROR_CHECK(leds_set_mode(DISPLAY_CONFIG_ERROR));
        vTaskSuspend(NULL);
    }
    nvs_init();
    QueueHandle_t queue = xQueueCreate(1, sizeof(app_status_t));
    ESP_ERROR_CHECK(queue ? ESP_OK : ESP_ERR_NO_MEM);
    wifi_init();
    ESP_ERROR_CHECK(web_server_start());
    ESP_ERROR_CHECK(graph_client_init(queue));
    app_status_t status = {.service = SERVICE_CONNECTING, .presence = PRESENCE_UNKNOWN};
    display_mode_t previous = DISPLAY_MODE_COUNT;
    TickType_t refresh_ticks = task_ticks_ms(100);
    int64_t last_diagnostic = 0;
    for (;;) {
        diagnostics_sample("main", &last_diagnostic);
        app_status_t received;
        if (xQueueReceive(queue, &received, refresh_ticks) == pdTRUE)
            status = received;
        display_mode_t mode = app_display_mode(&status, wifi_is_connected(), esp_timer_get_time(),
                                               (int64_t)CONFIG_PRESENCE_STALE_SECONDS * 1000000);
        if (mode != previous) {
            ESP_ERROR_CHECK(leds_set_mode(mode));
            ESP_LOGI("main", "Display mode: %d", (int)mode);
            previous = mode;
        }
    }
}
