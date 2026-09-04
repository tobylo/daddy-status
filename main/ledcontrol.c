#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "led_strip.h"
#include "ledcontrol.h"

static const char* TAG = "ledcontrol";
static TaskHandle_t led_task_handle = NULL;

static const int BLINK_INTERVAL = 750;
static struct led_color_t *DESIRED_COLORS[LED_STRIP_LENGTH];

static led_strip_handle_t strip;

struct led_color_t LED_COLOR_OFF = {
    .red = 0,
    .green = 0,
    .blue = 0
};

struct led_color_t LED_COLOR_YELLOW = {
    .red = 255,
    .green = 255,
    .blue = 0
};

struct led_color_t LED_COLOR_RED = {
    .red = 180,
    .green = 0,
    .blue = 0
};

struct led_color_t LED_COLOR_GREEN = {
    .red = 0,
    .green = 140,
    .blue = 0
};

const uint8_t hsv_lookup[121] = {
    0, 2, 4, 6, 8, 11, 13, 15, 17, 19, 21, 23, 25, 28, 30, 32, 34, 36, 38, 40,
    42, 45, 47, 49, 51, 53, 55, 57, 59, 62, 64, 66, 68, 70, 72, 74, 76, 79, 81, 
    83, 85, 87, 89, 91, 93, 96, 98, 100, 102, 104, 106, 108, 110, 113, 115, 117, 
    119, 121, 123, 125, 127, 130, 132, 134, 136, 138, 140, 142, 144, 147, 149, 
    151, 153, 155, 157, 159, 161, 164, 166, 168, 170, 172, 174, 176, 178, 181, 
    183, 185, 187, 189, 191, 193, 195, 198, 200, 202, 204, 206, 208, 210, 212, 
    215, 217, 219, 221, 223, 225, 227, 229, 232, 234, 236, 238, 240, 242, 244, 
    246, 249, 251, 253, 255
};

/* A persistent peripheral failure is fatal; do not leave an infinite retry loop. */
static void pixel_checked(int index, const struct led_color_t *color)
{
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < 3; ++attempt) {
        err = led_strip_set_pixel(strip, index, color->red, color->green, color->blue);
        if (err == ESP_OK) return;
        ESP_LOGW(TAG, "LED pixel update failed: %s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    ESP_ERROR_CHECK(err);
}

static void strip_checked(bool clear)
{
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < 3; ++attempt) {
        err = clear ? led_strip_clear(strip) : led_strip_refresh(strip);
        if (err == ESP_OK) return;
        ESP_LOGW(TAG, "LED output failed: %s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    ESP_ERROR_CHECK(err);
}

static void hsv_angle_to_rgb(int angle, struct led_color_t *color, float intensity)
{
    if (angle < 120)
    {
        color->red = hsv_lookup[120 - angle];
        color->green = hsv_lookup[angle];
        color->blue = 0;
    }
    else if (angle < 240)
    {
        color->red = 0;
        color->green = hsv_lookup[240 - angle];
        color->blue = hsv_lookup[angle - 120];
    }
    else
    {
        color->red = hsv_lookup[angle - 240];
        color->green = 0;
        color->blue = hsv_lookup[360 - angle];
    }
}

static void leds_rainbow_task(void *pvParameters)
{
    ESP_LOGD(TAG, "called: leds_rainbow_task");
    for(;;)
    {
        struct led_color_t color;
        for(int k = 0; k<360; k++)
        {
            hsv_angle_to_rgb(k, &color, 0.3);
            for (int i = 0; i < LED_STRIP_LENGTH; i++) {
                pixel_checked(i, &color);
            }
            strip_checked(false);
            vTaskDelay(15 / portTICK_PERIOD_MS);
        }
    }   
}

static void leds_helper_stop_task(void)
{
    ESP_LOGD(TAG, "called: leds_helper_stop_task");
    TaskHandle_t xTask = led_task_handle;
    if( led_task_handle != NULL )
    {
        led_task_handle = NULL;
        ESP_LOGI(TAG, "deleting led_task");
        vTaskDelete( xTask );
    }
}

void leds_blink_task(void *pvParameters)
{
    ESP_LOGD(TAG, "called: leds_blink_task");
    for(;;)
    {
        for (int i = 0; i < LED_STRIP_LENGTH; i++) {
            pixel_checked(i, DESIRED_COLORS[i]);
        }
        strip_checked(false);
        vTaskDelay(BLINK_INTERVAL / portTICK_PERIOD_MS);
        
        strip_checked(true);
        strip_checked(false);
        vTaskDelay(BLINK_INTERVAL / portTICK_PERIOD_MS);
    }
}

void leds_clear(void)
{
    ESP_LOGD(TAG, "called: leds_clear");

    leds_helper_stop_task();
    strip_checked(true);
    for (int i = 0; i < LED_STRIP_LENGTH; i++) {
        DESIRED_COLORS[i] = &LED_COLOR_OFF;
    }
    leds_apply(false);
}

bool leds_init(void)
{
    const led_strip_config_t config = {
        .strip_gpio_num = CONFIG_LED_DATA_GPIO,
        .max_leds = LED_STRIP_LENGTH,
        .led_model = LED_MODEL_WS2812,
    };
    const led_strip_rmt_config_t rmt = {.resolution_hz = 10000000};
    bool led_init_ok = led_strip_new_rmt_device(&config, &rmt, &strip) == ESP_OK;
    if (!led_init_ok) return false;

    leds_clear();
    xTaskCreate(leds_rainbow_task, "leds_rainbow_task", 2048, NULL, 5, &led_task_handle);

    return led_init_ok;
}

void led_color(int led_index, struct led_color_t *color)
{
    ESP_LOGD(TAG, "called: led_color");

    if(led_index < 0 || led_index >= LED_STRIP_LENGTH || color == NULL) {
        ESP_LOGE(TAG, "tried to set led color for index %d, array only contains %d leds", led_index, LED_STRIP_LENGTH);
        return;
    }

    DESIRED_COLORS[led_index] = color;
    pixel_checked(led_index, color);
}

void leds_color(struct led_color_t *color)
{
    ESP_LOGD(TAG, "called: leds_color");
    if (color == NULL) {
        ESP_LOGE(TAG, "Null LED color");
        return;
    }

    for (int i = 0; i < LED_STRIP_LENGTH; i++)
    {
        DESIRED_COLORS[i] = color;
        pixel_checked(i, color);
    }
}

void leds_rainbow(void)
{
    ESP_LOGD(TAG, "called: leds_rainbow");
    xTaskCreate(leds_rainbow_task, "leds_rainbow_task", 2048, NULL, 5, &led_task_handle);
}

void leds_apply(bool flash)
{
    ESP_LOGD(TAG, "called: leds_apply with %s", flash ? "TRUE" : "FALSE");
    if(flash) {
        xTaskCreate(leds_blink_task, "leds_helper_blink_task", 2048, NULL, 5, &led_task_handle);
    } else {
        strip_checked(false);

        // add delay to give the leds a change to update
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}