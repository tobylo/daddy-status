#include "app_state.h"

display_mode_t app_display_mode(const app_status_t *status, bool connected,
                                int64_t now_us, int64_t stale_after_us)
{
    if (!status) return DISPLAY_UNKNOWN;
    if (status->service == SERVICE_CONFIG_ERROR) return DISPLAY_CONFIG_ERROR;
    if (!connected) return DISPLAY_CONNECTING;
    bool fresh = status->has_presence && stale_after_us > 0 &&
        now_us >= status->updated_at_us && now_us - status->updated_at_us < stale_after_us;
    if (fresh) {
        switch (status->presence) {
            case PRESENCE_AVAILABLE: return DISPLAY_AVAILABLE;
            case PRESENCE_BUSY: return DISPLAY_BUSY;
            case PRESENCE_DO_NOT_DISTURB: return DISPLAY_DO_NOT_DISTURB;
            case PRESENCE_OFF_WORK: return DISPLAY_PLAY;
            default: return DISPLAY_UNKNOWN;
        }
    }
    if (!status->has_presence && status->service == SERVICE_AUTHENTICATING) return DISPLAY_AUTHENTICATING;
    return DISPLAY_UNKNOWN;
}
