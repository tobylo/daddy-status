#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "led_strip/led_strip.h"
#include "ledcontrol.h"

static const char* TAG = "ledcontrol";

bool init_leds()
{
    led_strip.access_semaphore = xSemaphoreCreateBinary();
    bool led_init_ok = led_strip_init(&led_strip);
    printf("Led strip is %s\n", led_init_ok ? "OK" : "Not OK");
    assert(led_init_ok);
    return led_init_ok;
}

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

void hsv_angle_to_rgb(int angle, struct led_color_t *color, float intensity)
{
    if (angle < 120)
    {
        color->red = hsv_lookup[120 - angle]*intensity;
        color->green = hsv_lookup[angle]*intensity;
        color->blue = 0;
    }
    else if (angle < 240)
    {
        color->red = 0;
        color->green = hsv_lookup[240 - angle]*intensity;
        color->blue = hsv_lookup[angle - 120]*intensity;
    }
    else
    {
        color->red = hsv_lookup[angle - 240]*intensity;
        color->green = 0;
        color->blue = hsv_lookup[360 - angle]*intensity;
    }
}

void leds_blink_random()
{
    printf("inside leds_blink_random");
    while (1)
    {
        for (int i = 0; i < LED_STRIP_LENGTH; i++)
        {
            struct led_color_t random = {
                .red = esp_random() % 17,
                .green = esp_random() % 17,
                .blue = esp_random() % 17};
            led_strip_set_pixel_color(&led_strip, i, &random);
            printf("Led %d shows R:%d G:%d B:%d\n", i, random.red, random.green, random.blue);
        }

        led_strip_show(&led_strip);
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

void leds_blink(struct led_color_t *color)
{
    while(1)
    {
        for (int i = 0; i < LED_STRIP_LENGTH; i++)
        {
            led_strip_set_pixel_color(&led_strip, i, color);
        }
        led_strip_show(&led_strip);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        leds_clear();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void leds_rainbow()
{
    while(1)
    {
        struct led_color_t color;
        for(int k = 0; k<360; k++)
        {
            ESP_LOGI(TAG, "Running k: %d", k);
            hsv_angle_to_rgb(k, &color, 0.3);
            led_strip_set_pixel_color(&led_strip, 0, &color);
            led_strip_set_pixel_color(&led_strip, 1, &color);
            led_strip_show(&led_strip);
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }    
}

void leds_clear()
{
    led_strip_clear(&led_strip);
    led_strip_show(&led_strip);
}