#ifndef DADDY_APP_STATE_H
#define DADDY_APP_STATE_H
#include "presence.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SERVICE_CONNECTING,
    SERVICE_SYNCING_CLOCK,
    SERVICE_AUTHENTICATING,
    SERVICE_POLLING,
    SERVICE_READY,
    SERVICE_ERROR,
    SERVICE_CONFIG_ERROR,
} service_state_t;

typedef struct {
    service_state_t service;
    presence_t presence;
    bool has_presence;
    int64_t updated_at_us;
} app_status_t;

typedef enum {
    DISPLAY_CONNECTING,
    DISPLAY_AUTHENTICATING,
    DISPLAY_UNKNOWN,
    DISPLAY_AVAILABLE,
    DISPLAY_BUSY,
    DISPLAY_DO_NOT_DISTURB,
    DISPLAY_PLAY,
    DISPLAY_CONFIG_ERROR,
    DISPLAY_MODE_COUNT,
} display_mode_t;

display_mode_t app_display_mode(const app_status_t *status, bool connected, int64_t now_us,
                                int64_t stale_after_us);
#endif
