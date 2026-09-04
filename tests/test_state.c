#include "app_state.h"
#include "led_frame.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    app_status_t status = {.service = SERVICE_CONNECTING, .presence = PRESENCE_UNKNOWN};
    const int64_t ttl = 60000000;
    assert(app_display_mode(&status, false, 0, ttl) == DISPLAY_CONNECTING);
    assert(app_display_mode(&status, true, 0, ttl) == DISPLAY_UNKNOWN);
    status.service = SERVICE_AUTHENTICATING;
    assert(app_display_mode(&status, true, 0, ttl) == DISPLAY_AUTHENTICATING);
    status.has_presence = true;
    status.presence = PRESENCE_DO_NOT_DISTURB;
    status.updated_at_us = 100;
    assert(app_display_mode(&status, true, 100, ttl) == DISPLAY_DO_NOT_DISTURB);
    status.service = SERVICE_ERROR;
    assert(app_display_mode(&status, true, ttl + 99, ttl) == DISPLAY_DO_NOT_DISTURB);
    assert(app_display_mode(&status, true, ttl + 100, ttl) == DISPLAY_UNKNOWN);
    assert(app_display_mode(&status, false, 100, ttl) == DISPLAY_CONNECTING);
    assert(app_display_mode(&status, true, 99, ttl) == DISPLAY_UNKNOWN);
    status.updated_at_us = ttl + 100;
    status.service = SERVICE_READY;
    assert(app_display_mode(&status, true, ttl + 101, ttl) == DISPLAY_DO_NOT_DISTURB);
    const presence_t presences[] = {PRESENCE_AVAILABLE, PRESENCE_BUSY, PRESENCE_OFF_WORK};
    const display_mode_t modes[] = {DISPLAY_AVAILABLE, DISPLAY_BUSY, DISPLAY_PLAY};
    for (unsigned i = 0; i < 3; ++i) {
        status.presence = presences[i];
        assert(app_display_mode(&status, true, ttl + 101, ttl) == modes[i]);
    }
    status.presence = PRESENCE_UNKNOWN;
    assert(app_display_mode(&status, true, ttl + 101, ttl) == DISPLAY_UNKNOWN);
    status.service = SERVICE_CONFIG_ERROR;
    assert(app_display_mode(&status, false, 0, ttl) == DISPLAY_CONFIG_ERROR);
    led_rgb_t frame[STATUS_LED_COUNT];
    assert(led_frame(DISPLAY_DO_NOT_DISTURB, 0, 100, frame) && frame[0].red == 180);
    assert(led_frame(DISPLAY_DO_NOT_DISTURB, 750, 100, frame) && frame[0].red == 0);
    assert(led_frame(DISPLAY_DO_NOT_DISTURB, 1500, 100, frame) && frame[1].red == 180);
    assert(led_frame(DISPLAY_BUSY, 1000, 100, frame) && frame[0].red == 255 && frame[1].red == 0);
    assert(led_frame(DISPLAY_AVAILABLE, 0, 50, frame) && frame[0].green == 70);
    assert(led_frame(DISPLAY_UNKNOWN, 0, 100, frame) && frame[0].blue == 140 &&
           frame[0].green == 0);
    assert(led_frame(DISPLAY_PLAY, 0, 100, frame) && frame[0].red == 76);
    assert(led_frame(DISPLAY_PLAY, 1800, 100, frame) && frame[0].green == 76);
    assert(led_frame(DISPLAY_PLAY, 3600, 100, frame) && frame[0].blue == 76);
    assert(!led_frame(DISPLAY_MODE_COUNT, 0, 100, frame));
    assert(!led_frame(DISPLAY_AVAILABLE, 0, 101, frame));
    assert(!led_frame(DISPLAY_AVAILABLE, 0, 100, NULL));
    /* Rapid changes are complete independent frames, with no stale color pointers. */
    for (unsigned i = 0; i < 10000; ++i) {
        assert(led_frame(DISPLAY_PLAY, i * 15, 100, frame));
        assert(led_frame(DISPLAY_BUSY, 0, 100, frame));
        assert(frame[1].red == 0 && frame[1].green == 0 && frame[1].blue == 0);
        assert(led_frame(DISPLAY_AVAILABLE, 0, 100, frame));
        assert(frame[0].red == 0 && frame[0].blue == 0 && frame[1].green == 140);
    }
    puts("state and LED frame tests passed");
}
