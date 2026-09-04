#include "ledcontrol.h"
#include "led_frame.h"
#include "led_strip.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static struct test_queue { bool allocated, pending; display_mode_t mode; } queue;
static bool fail_queue, fail_driver, fail_task;
static unsigned created_tasks, deleted_strips, refreshes, event_step;
static int64_t now;
static void (*worker_fn)(void *);
static led_rgb_t pixels[STATUS_LED_COUNT];
static jmp_buf finished;

QueueHandle_t xQueueCreate(unsigned length, size_t size)
{
    assert(length == 1 && size == sizeof(display_mode_t) && !queue.allocated);
    if (fail_queue) return NULL;
    queue.allocated = true; queue.pending = false;
    return &queue;
}
void vQueueDelete(QueueHandle_t q) { assert(q == &queue); queue.allocated = false; }
BaseType_t xQueueOverwrite(QueueHandle_t q, const void *item)
{
    assert(q == &queue && queue.allocated);
    memcpy(&queue.mode, item, sizeof(queue.mode)); queue.pending = true;
    return pdPASS;
}
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait)
{
    assert(q == &queue && queue.allocated);
    if (!queue.pending) {
        switch (event_step++) {
            case 0:
                assert(refreshes == 2); /* Connecting frame, then DND. */
                assert(pixels[0].red == 180);
                assert(leds_set_mode(DISPLAY_BUSY) == ESP_OK);
                break;
            case 1:
                assert(refreshes == 3 && wait == 250); /* Retry the failed output. */
                now += (int64_t)wait * 1000;
                return pdFALSE;
            case 2:
                assert(refreshes == 4 && wait == portMAX_DELAY);
                assert(leds_set_mode(DISPLAY_AVAILABLE) == ESP_OK);
                break;
            default:
                assert(refreshes == 5 && pixels[0].green == 140 && pixels[1].green == 140);
                assert(wait == portMAX_DELAY && created_tasks == 1);
                longjmp(finished, 1);
        }
    }
    memcpy(item, &queue.mode, sizeof(queue.mode)); queue.pending = false;
    return pdTRUE;
}
BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                       void *arg, unsigned priority, TaskHandle_t *handle)
{
    (void)name; (void)stack; (void)arg; (void)priority; (void)handle;
    if (fail_task) return pdFAIL;
    worker_fn = task; ++created_tasks;
    return pdPASS;
}
int64_t esp_timer_get_time(void) { return now; }
esp_err_t led_strip_new_rmt_device(const led_strip_config_t *config,
                                  const led_strip_rmt_config_t *rmt, led_strip_handle_t *strip)
{
    assert(config->max_leds == STATUS_LED_COUNT && config->strip_gpio_num == 25);
    assert(rmt->resolution_hz == 10000000);
    if (fail_driver) return ESP_FAIL;
    *strip = &pixels;
    return ESP_OK;
}
esp_err_t led_strip_set_pixel(led_strip_handle_t strip, unsigned index,
                             uint32_t red, uint32_t green, uint32_t blue)
{
    assert(strip == &pixels && index < STATUS_LED_COUNT);
    pixels[index] = (led_rgb_t){red, green, blue};
    return ESP_OK;
}
esp_err_t led_strip_refresh(led_strip_handle_t strip)
{
    assert(strip == &pixels);
    ++refreshes;
    return refreshes == 3 ? ESP_FAIL : ESP_OK;
}
esp_err_t led_strip_del(led_strip_handle_t strip) { assert(strip == &pixels); ++deleted_strips; return ESP_OK; }

int main(void)
{
    assert(leds_set_mode(DISPLAY_AVAILABLE) == ESP_ERR_INVALID_STATE);
    fail_queue = true; assert(leds_init() == ESP_ERR_NO_MEM); fail_queue = false;
    fail_driver = true; assert(leds_init() == ESP_FAIL && !queue.allocated); fail_driver = false;
    fail_task = true; assert(leds_init() == ESP_ERR_NO_MEM && !queue.allocated && deleted_strips == 1); fail_task = false;
    assert(leds_init() == ESP_OK && created_tasks == 1);
    assert(leds_init() == ESP_ERR_INVALID_STATE);
    assert(leds_set_mode(DISPLAY_MODE_COUNT) == ESP_ERR_INVALID_ARG);
    assert(leds_set_mode(DISPLAY_PLAY) == ESP_OK);
    assert(leds_set_mode(DISPLAY_DO_NOT_DISTURB) == ESP_OK); /* Latest mode wins. */
    if (!setjmp(finished)) worker_fn(NULL);
    puts("LED worker queue, initialization, retry, and mode-change tests passed");
}
