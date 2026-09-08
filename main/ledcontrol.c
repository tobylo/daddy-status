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
#include "settings.h"
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

typedef struct {
    led_rgb_t previous[STATUS_LED_COUNT];
    bool rendered;
    bool failed;
} render_state_t;

static void record_render_failure(render_state_t *state, esp_err_t err)
{
    if (!state->failed)
        ESP_LOGE(TAG, "LED output failed: %s; retrying", esp_err_to_name(err));
    state->failed = true;
    state->rendered = false;
}

static void record_render_success(render_state_t *state,
                                  const led_rgb_t frame[STATUS_LED_COUNT])
{
    memcpy(state->previous, frame, sizeof(state->previous));
    state->rendered = true;
    if (state->failed)
        ESP_LOGI(TAG, "LED output recovered");
    state->failed = false;
}

/* Cache only successful output; failed writes must be retried even for identical pixels. */
static void render_if_changed(display_mode_t mode, uint64_t elapsed_ms, render_state_t *state)
{
    led_rgb_t frame[STATUS_LED_COUNT];
    if (!led_frame(mode, elapsed_ms, settings_brightness(), frame))
        return;
    if (state->rendered && memcmp(state->previous, frame, sizeof(frame)) == 0)
        return;

    esp_err_t err = render(frame);
    if (err != ESP_OK)
        record_render_failure(state, err);
    else
        record_render_success(state, frame);
}

static TickType_t next_wait_ticks(display_mode_t mode, bool failed)
{
    if (failed)
        return task_ticks_ms(250);
    return led_mode_animated(mode) ? task_ticks_ms(15) : portMAX_DELAY;
}

/* Refresh notifications wake the task without restarting the current animation. */
static bool take_mode_change(display_mode_t *mode, int64_t *started, TickType_t wait)
{
    display_mode_t next;
    if (xQueueReceive(mode_queue, &next, wait) != pdTRUE)
        return false;
    if ((unsigned)next >= DISPLAY_MODE_COUNT || next == *mode)
        return false;
    *mode = next;
    *started = esp_timer_get_time();
    return true;
}

/* Only this task accesses the driver after initialization. Never delete it to change modes. */
static void led_task(void *unused)
{
    display_mode_t mode = DISPLAY_CONNECTING;
    int64_t started = esp_timer_get_time();
    render_state_t state = {0};
    int64_t last_diagnostic = 0;
    for (;;) {
        diagnostics_sample("leds", &last_diagnostic);
        uint64_t elapsed_ms = (esp_timer_get_time() - started) / 1000;
        render_if_changed(mode, elapsed_ms, &state);
        if (take_mode_change(&mode, &started, next_wait_ticks(mode, state.failed)))
            state.rendered = false;
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

void leds_refresh(void)
{
    if (!mode_queue)
        return;
    /* A queued mode already wakes the worker. Never overwrite it with a refresh. */
    display_mode_t refresh = DISPLAY_MODE_COUNT;
    xQueueSend(mode_queue, &refresh, 0);
}
