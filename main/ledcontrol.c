#include "ledcontrol.h"
#include "diagnostics.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "led_frame.h"
#include "led_strip.h"
#include "sdkconfig.h"
#include "task_time.h"
#include <string.h>

static const char *TAG = "leds";
static QueueHandle_t mode_queue;
static led_strip_handle_t strip;

static esp_err_t render(const led_rgb_t frame[STATUS_LED_COUNT])
{
    for (unsigned i = 0; i < STATUS_LED_COUNT; ++i) {
        esp_err_t err = led_strip_set_pixel(strip, i, frame[i].red, frame[i].green, frame[i].blue);
        if (err != ESP_OK)
            return err;
    }
    return led_strip_refresh(strip);
}

/* Only this task accesses the driver after initialization. Never delete it to change modes. */
static void led_task(void *unused)
{
    display_mode_t mode = DISPLAY_CONNECTING;
    int64_t started = esp_timer_get_time();
    led_rgb_t previous[STATUS_LED_COUNT] = {0};
    bool rendered = false, failed = false;
    int64_t last_diagnostic = 0;
    for (;;) {
        diagnostics_sample("leds", &last_diagnostic);
        led_rgb_t frame[STATUS_LED_COUNT];
        uint64_t elapsed_ms = (esp_timer_get_time() - started) / 1000;
        if (led_frame(mode, elapsed_ms, CONFIG_LED_BRIGHTNESS_PERCENT, frame) &&
            (!rendered || memcmp(previous, frame, sizeof(frame)))) {
            esp_err_t err = render(frame);
            if (err == ESP_OK) {
                memcpy(previous, frame, sizeof(frame));
                rendered = true;
                if (failed)
                    ESP_LOGI(TAG, "LED output recovered");
                failed = false;
            } else {
                if (!failed)
                    ESP_LOGE(TAG, "LED output failed: %s; retrying", esp_err_to_name(err));
                failed = true;
                rendered = false;
            }
        }
        TickType_t wait = failed ? task_ticks_ms(250)
                                 : (led_mode_animated(mode) ? task_ticks_ms(15) : portMAX_DELAY);
        display_mode_t next;
        if (xQueueReceive(mode_queue, &next, wait) == pdTRUE && next != mode) {
            mode = next;
            started = esp_timer_get_time();
            rendered = false;
        }
    }
}

esp_err_t leds_init(void)
{
    if (mode_queue)
        return ESP_ERR_INVALID_STATE;
    mode_queue = xQueueCreate(1, sizeof(display_mode_t));
    if (!mode_queue)
        return ESP_ERR_NO_MEM;
    const led_strip_config_t config = {
        .strip_gpio_num = CONFIG_LED_DATA_GPIO,
        .max_leds = STATUS_LED_COUNT,
        .led_model = LED_MODEL_WS2812,
    };
    const led_strip_rmt_config_t rmt = {.resolution_hz = 10000000};
    esp_err_t err = led_strip_new_rmt_device(&config, &rmt, &strip);
    if (err == ESP_OK && xTaskCreate(led_task, "leds", 4096, NULL, 5, NULL) != pdPASS) {
        led_strip_del(strip);
        strip = NULL;
        err = ESP_ERR_NO_MEM;
    }
    if (err != ESP_OK) {
        vQueueDelete(mode_queue);
        mode_queue = NULL;
    }
    return err;
}

esp_err_t leds_set_mode(display_mode_t mode)
{
    if (mode < 0 || mode >= DISPLAY_MODE_COUNT)
        return ESP_ERR_INVALID_ARG;
    if (!mode_queue)
        return ESP_ERR_INVALID_STATE;
    return xQueueOverwrite(mode_queue, &mode) == pdPASS ? ESP_OK : ESP_FAIL;
}
