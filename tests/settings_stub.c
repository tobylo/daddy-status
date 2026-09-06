#include "sdkconfig.h"
#include "settings.h"
static const frame_settings_t value = {.ssid = CONFIG_WIFI_SSID,
                                       .password = CONFIG_WIFI_PASSWORD,
                                       .tenant = CONFIG_AAD_TENANT_ID,
                                       .client = CONFIG_AAD_CLIENT_ID,
                                       .ntp = CONFIG_NTP_SERVER,
                                       .poll_seconds = CONFIG_PRESENCE_POLL_SECONDS,
                                       .stale_seconds = CONFIG_PRESENCE_STALE_SECONDS,
                                       .brightness = CONFIG_LED_BRIGHTNESS_PERCENT};
const frame_settings_t *settings_get(void)
{
    return &value;
}
