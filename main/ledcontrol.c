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
static bool task_running = false;

static const int BLINK_INTERVAL = 750;
static struct led_color_t *DESIRED_COLORS[LED_STRIP_LENGTH];

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

    task_running = true;
    ESP_LOGD(TAG, "running rainbow leds");
    for(;;)
    {
        struct led_color_t color;
        for(int k = 0; k<360; k++)
        {
            hsv_angle_to_rgb(k, &color, 0.3);
            for (int i = 0; i < LED_STRIP_LENGTH; i++) {
                led_strip_set_pixel_color(&led_strip, i, &color);
            }
            led_strip_show(&led_strip);
            vTaskDelay(15 / portTICK_PERIOD_MS);
        }
    }   
}

static void leds_helper_stop_task()
{
    ESP_LOGD(TAG, "called: leds_helper_stop_task");

    if(task_running == true) {
        ESP_LOGD(TAG, "leds_clear: task_running is true");
        vTaskDelete( led_task_handle );
        task_running = false;
    }
}

void leds_blink_task(void *pvParameters)
{
    ESP_LOGD(TAG, "called: leds_blink_task");
    task_running = true;
    for(;;)
    {
        for (int i = 0; i < LED_STRIP_LENGTH; i++) {
            led_strip_set_pixel_color(&led_strip, i, DESIRED_COLORS[i]);
        }
        led_strip_show(&led_strip);
        vTaskDelay(BLINK_INTERVAL / portTICK_PERIOD_MS);
        
        led_strip_clear(&led_strip);
        led_strip_show(&led_strip);
        vTaskDelay(BLINK_INTERVAL / portTICK_PERIOD_MS);
    }
}

void leds_clear()
{
    ESP_LOGD(TAG, "called: leds_clear");

    leds_helper_stop_task();
    led_strip_clear(&led_strip);
    for (int i = 0; i < LED_STRIP_LENGTH; i++) {
        DESIRED_COLORS[i] = &LED_COLOR_OFF;
    }
    leds_apply(false);
}

bool leds_init()
{
    led_strip.access_semaphore = xSemaphoreCreateBinary();
    bool led_init_ok = led_strip_init(&led_strip);
    ESP_LOGI(TAG, "led trip initialisation: %s", led_init_ok ? "SUCCESSFUL" : "ERROR");
    assert(led_init_ok);

    leds_clear();
    xTaskCreate(leds_rainbow_task, "leds_rainbow_task", 2048, NULL, 5, &led_task_handle);

    return led_init_ok;
}

void led_color(int led_index, struct led_color_t *color)
{
    ESP_LOGD(TAG, "called: led_color");

    if(led_index >= LED_STRIP_LENGTH) {
        ESP_LOGE(TAG, "tried to set led color for index %d, array only contains %d leds", led_index, LED_STRIP_LENGTH);
        return;
    }

    DESIRED_COLORS[led_index] = color;
    while(led_strip_set_pixel_color(&led_strip, led_index, DESIRED_COLORS[led_index]) == false) {
        ESP_LOGE(TAG, "led_strip_set_pixel_color: failed, retrying..");
        vTaskDelay(250 / portTICK_PERIOD_MS);
    }
}

void leds_color(struct led_color_t *color)
{
    ESP_LOGD(TAG, "called: leds_color");

    for (int i = 0; i < LED_STRIP_LENGTH; i++)
    {
        DESIRED_COLORS[i] = color;
        while(led_strip_set_pixel_color(&led_strip, i, DESIRED_COLORS[i]) == false) {
            ESP_LOGE(TAG, "led_strip_set_pixel_color: failed, retrying..");
            vTaskDelay(250 / portTICK_PERIOD_MS);
        }
    }
}

void leds_rainbow()
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
        while(led_strip_show(&led_strip) == false) {
            ESP_LOGE(TAG, "leds_apply: failed, retrying..");
            vTaskDelay(250 / portTICK_PERIOD_MS);
        }
        
        // add delay to give the leds a change to update
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}