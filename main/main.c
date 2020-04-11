#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "ledcontrol.h"
#include "gpio.h"
#include "interrupt.h"
#include "timer.h"
#include "nvs_flash.h"
#include "http_client.h"
#include "wifi.h"

#define ESP_INTR_FLAG_DEFAULT 0

static const char* TAG = "main";
static xQueueHandle evt_queue = NULL;

esp_err_t nvs_init()
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );
    return ret;
}

esp_err_t pm_init()
{
#if CONFIG_PM_ENABLE
    // Configure dynamic frequency scaling:
    // maximum and minimum frequencies are set in sdkconfig,
    // automatic light sleep is enabled if tickless idle support is enabled.
    esp_pm_config_esp32_t pm_config = {
            .max_freq_mhz = CONFIG_EXAMPLE_MAX_CPU_FREQ_MHZ,
            .min_freq_mhz = CONFIG_EXAMPLE_MIN_CPU_FREQ_MHZ,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
            .light_sleep_enable = true
#endif
    };
    return ESP_ERROR_CHECK( esp_pm_configure(&pm_config) );
#endif // CONFIG_PM_ENABLE
    
    return ESP_OK;
}

void app_main()
{
    leds_init();

    nvs_init();
    pm_init();

    wifi_init();
    wifi_wait_connected();

    graph_client_init();

    xTaskCreate(&http_test_task, "http_test_task", 8192, NULL, 5, NULL);
    xTaskCreate(&leds_rainbow_task, "leds_rainbow_task", 8192, NULL, 5, NULL);

    //leds_blink_random();
    //leds_rainbow();
    //leds_blink_alert();

    ESP_LOGI(TAG, "All initialized");
    
    /*
    while(1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        gps_retrieve();
    }
    */
}