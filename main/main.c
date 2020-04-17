#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "ledcontrol.h"
#include "nvs_flash.h"
#include "wifi.h"
#include "graph_client.h"

#define ESP_INTR_FLAG_DEFAULT 0

static const char* TAG = "main";
static xQueueHandle evt_queue = NULL;
static unsigned int current = 1000;

esp_err_t nvs_init()
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG, "no nvs pages or new version found, erasing flash");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );
    return ret;
}

esp_err_t queue_init()
{
    evt_queue = xQueueCreate(1, sizeof(uint32_t));
    if(evt_queue != NULL) {
        return ESP_OK;
    } else {
        return ESP_FAIL;
    }
}

void presence_handler_task(void *pvParameters)
{
    QueueHandle_t *queue = pvParameters;

    ESP_LOGI(TAG, "Retrieve task started..");
    unsigned int presence;
    for(;;) 
    {
        if(xQueueReceive(*queue, &presence, portMAX_DELAY)) 
        {
            ESP_LOGD(TAG, "received presence event");
            if(presence == current) {
                ESP_LOGD(TAG, "daddy status unchanged..");
            } else if(presence == PRESENCE_AVAILABLE) {
                ESP_LOGD(TAG, "daddy status: available");
                current = presence;
                leds_color(LED_COLOR_GREEN);
                leds_apply(false);
            } else if(presence == PRESENCE_BUSY) {
                ESP_LOGD(TAG, "daddy status: busy");
                current = presence;
                led_color(0, LED_COLOR_YELLOW);
                led_color(1, LED_COLOR_OFF);
                leds_apply(false);
            } else if(presence == PRESENCE_OFF_WORK) {
                ESP_LOGD(TAG, "Daddy status: play time");
                current = presence;
                leds_rainbow();
            } else { //if(presence == PRESENCE_DO_NOT_DISTURB)
                ESP_LOGD(TAG, "daddy status: DND");
                current = presence;
                leds_color(LED_COLOR_RED);
                leds_apply(true);
            }
        }
    }
}

void app_main()
{
    leds_init();

    nvs_init();

    wifi_init();
    wifi_wait_connected();
    
    queue_init();

    graph_client_init(&evt_queue);
    xTaskCreate(presence_handler_task, "presence_handler_task", 8192, &evt_queue, 5, NULL);

    ESP_LOGI(TAG, "All initialized");
}