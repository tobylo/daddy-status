#ifndef DADDY_SETTINGS_H
#define DADDY_SETTINGS_H
#include "cJSON.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    char ssid[33], password[65], tenant[37], client[37], ntp[254];
    int poll_seconds, stale_seconds, brightness;
} frame_settings_t;
/* Initialized before workers; this snapshot is immutable until restart. */
esp_err_t settings_init(void);
const frame_settings_t *settings_get(void);
/* Live brightness is synchronized separately from the immutable boot snapshot. */
int settings_brightness(void);
void settings_defaults(frame_settings_t *value);
bool settings_valid(const frame_settings_t *value, bool complete);
/* Parse a complete form; omitted/empty password keeps the existing secret.
 * Explicit open_network=true clears it. No secrets are returned by settings_json. */
/* Returns the first invalid field name, or NULL on success. */
const char *settings_parse_error(const cJSON *json, frame_settings_t *value);
bool settings_parse(const cJSON *json, frame_settings_t *value);
cJSON *settings_json(void);
typedef enum {
    SETTINGS_UNCHANGED,
    SETTINGS_APPLIED,
    SETTINGS_RESTART,
    SETTINGS_WIFI_TRIAL,
} settings_save_result_t;
/* Serialized saves: no-op, live brightness, restart, or Wi-Fi trial.
 * Result is written only on success. Other runtime settings stay intact until reboot. */
esp_err_t settings_save(const frame_settings_t *value, settings_save_result_t *result);
esp_err_t settings_reset_auth(void);
/* Main task: promotes DHCP-successful trials or rolls back after three minutes.
 * Returns true when a scheduled reboot is due. */
bool settings_tick(bool online, int64_t now);
bool settings_trial(void);
/* Reserve settings against saves/restarts while writing firmware; reject trials. */
bool settings_update_begin(void);
void settings_update_end(void);
#endif
