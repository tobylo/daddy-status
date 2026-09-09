#include "diagnostics.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "firmware_update.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "graph_client.h"
#include "improv.h"
#include "ledcontrol.h"
#include "protocol.h"
#include "sdkconfig.h"
#include "settings.h"
#include "storage.h"
#include "task_time.h"
#include "web_server.h"
#include "wifi.h"

void app_main(void)
{
    ESP_ERROR_CHECK(storage_init());
    ESP_ERROR_CHECK(settings_init());
    ESP_ERROR_CHECK(leds_init());
    QueueHandle_t queue = xQueueCreate(1, sizeof(app_status_t));
    ESP_ERROR_CHECK(queue ? ESP_OK : ESP_ERR_NO_MEM);
    wifi_init();
    improv_serial_init();
    ESP_ERROR_CHECK(web_server_start());
    ESP_ERROR_CHECK(graph_client_init(queue));
    firmware_update_init();
    app_status_t status = {.service = SERVICE_CONNECTING, .presence = PRESENCE_UNKNOWN};
    display_mode_t previous = DISPLAY_MODE_COUNT;
    TickType_t refresh_ticks = task_ticks_ms(100);
    int64_t last_diagnostic = 0;
    for (;;) {
        diagnostics_sample("main", &last_diagnostic);
        int64_t now = esp_timer_get_time();
        firmware_update_tick(wifi_is_connected(), now);
        bool reboot_due = settings_tick(wifi_is_connected(), now);
        improv_serial_tick(now, reboot_due);
        if (reboot_due)
            esp_restart();
        app_status_t received;
        if (xQueueReceive(queue, &received, refresh_ticks) == pdTRUE)
            status = received;
        display_mode_t mode = app_display_mode(&status, wifi_is_connected(), esp_timer_get_time(),
                                               (int64_t)settings_get()->stale_seconds * 1000000);
        mode = web_server_display(mode, esp_timer_get_time());
        web_server_update(&status, wifi_is_connected(), mode);
        if (mode != previous) {
            ESP_ERROR_CHECK(leds_set_mode(mode));
            ESP_LOGI("main", "Display mode: %d", (int)mode);
            previous = mode;
        }
    }
}
