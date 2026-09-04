#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "ledcontrol.h"
#include "nvs_flash.h"
#include "wifi.h"
#include "graph_client.h"

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
    nvs_init();
    QueueHandle_t queue = xQueueCreate(1, sizeof(app_status_t));
    ESP_ERROR_CHECK(queue ? ESP_OK : ESP_ERR_NO_MEM);
    wifi_init();
    ESP_ERROR_CHECK(graph_client_init(queue));
    app_status_t status = {.service = SERVICE_CONNECTING, .presence = PRESENCE_UNKNOWN};
    display_mode_t previous = DISPLAY_MODE_COUNT;
    for (;;) {
        app_status_t received;
        if (xQueueReceive(queue, &received, pdMS_TO_TICKS(100)) == pdTRUE) status = received;
        display_mode_t mode = app_display_mode(&status, wifi_is_connected(), esp_timer_get_time(),
                                              (int64_t)CONFIG_PRESENCE_STALE_SECONDS * 1000000);
        if (mode != previous) {
            ESP_ERROR_CHECK(leds_set_mode(mode));
            ESP_LOGI("main", "Display mode: %d", (int)mode);
            previous = mode;
        }
    }
}
