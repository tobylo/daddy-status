#include "settings.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "protocol.h"
#include "sdkconfig.h"
#include "token_storage.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t version;
    frame_settings_t active, candidate;
    uint8_t pending, tried, reset_auth;
} settings_record_t;
static settings_record_t record;
static frame_settings_t current;
static SemaphoreHandle_t mutex;
static bool trial;
static int64_t reboot_at, boot_at, next_commit;

void settings_defaults(frame_settings_t *s)
{
    *s = (frame_settings_t){.poll_seconds = CONFIG_PRESENCE_POLL_SECONDS,
                            .stale_seconds = CONFIG_PRESENCE_STALE_SECONDS,
                            .brightness = CONFIG_LED_BRIGHTNESS_PERCENT};
    snprintf(s->ssid, sizeof(s->ssid), "%s", CONFIG_WIFI_SSID);
    snprintf(s->password, sizeof(s->password), "%s", CONFIG_WIFI_PASSWORD);
    snprintf(s->tenant, sizeof(s->tenant), "%s", CONFIG_AAD_TENANT_ID);
    snprintf(s->client, sizeof(s->client), "%s", CONFIG_AAD_CLIENT_ID);
    snprintf(s->ntp, sizeof(s->ntp), "%s", CONFIG_NTP_SERVER);
    if (!settings_valid(s, false)) {
        ESP_LOGW("settings", "Invalid compiled defaults; starting with empty recovery settings");
        *s = (frame_settings_t){
            .ntp = "pool.ntp.org", .poll_seconds = 10, .stale_seconds = 60, .brightness = 100};
    }
}

bool settings_valid(const frame_settings_t *s, bool complete)
{
    if (!s || !memchr(s->ssid, 0, sizeof(s->ssid)) ||
        !memchr(s->password, 0, sizeof(s->password)) || !memchr(s->tenant, 0, sizeof(s->tenant)) ||
        !memchr(s->client, 0, sizeof(s->client)) || !memchr(s->ntp, 0, sizeof(s->ntp)))
        return false;
    size_t n = strlen(s->password);
    if (n && (n < 8 || n > 64))
        return false;
    if (n && n < 64)
        for (size_t i = 0; i < n; ++i)
            if ((unsigned char)s->password[i] < 32 || (unsigned char)s->password[i] > 126)
                return false;
    if (n == 64)
        for (size_t i = 0; i < n; ++i)
            if (!isxdigit((unsigned char)s->password[i]))
                return false;
    if ((complete && !s->ssid[0]) || ((complete || s->tenant[0]) && !guid_valid(s->tenant)) ||
        ((complete || s->client[0]) && !guid_valid(s->client)) || !s->ntp[0])
        return false;
    for (const char *p = s->ntp; *p; ++p)
        if (!(isalnum((unsigned char)*p) || *p == '.' || *p == '-'))
            return false;
    return s->poll_seconds >= 2 && s->poll_seconds <= 300 && s->stale_seconds >= 30 &&
           s->stale_seconds <= 3600 && s->stale_seconds > s->poll_seconds && s->brightness >= 1 &&
           s->brightness <= 100;
}

static esp_err_t persist(const settings_record_t *next)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("frame", NVS_READWRITE, &h);
    if (err != ESP_OK)
        return err;
    err = nvs_set_blob(h, "settings", next, sizeof(*next));
    if (err == ESP_OK)
        err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK)
        record = *next;
    return err;
}

esp_err_t settings_init(void)
{
    mutex = xSemaphoreCreateMutex();
    if (!mutex)
        return ESP_ERR_NO_MEM;
    record = (settings_record_t){.version = 1};
    settings_defaults(&record.active);
    settings_record_t next = record;
    nvs_handle_t h;
    esp_err_t err = nvs_open("frame", NVS_READONLY, &h);
    if (err == ESP_OK) {
        size_t size = 0;
        err = nvs_get_blob(h, "settings", NULL, &size);
        if (err == ESP_OK && size == sizeof(next))
            err = nvs_get_blob(h, "settings", &next, &size);
        nvs_close(h);
        if (err == ESP_OK) {
            if (size == sizeof(next) && next.version == 1 && next.pending <= 1 && next.tried <= 1 &&
                next.reset_auth <= 1 && settings_valid(&next.active, false) &&
                (!next.pending || settings_valid(&next.candidate, true)))
                record = next;
            else
                ESP_LOGW("settings",
                         "Invalid saved settings; using compiled defaults for recovery");
        } else if (err == ESP_ERR_NVS_TYPE_MISMATCH) {
            ESP_LOGW("settings", "Invalid settings storage type; using compiled defaults");
            err = ESP_OK;
        }
    }
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        return err;
    next = record;
    trial = next.pending && !next.tried;
    if (next.pending && next.tried) {
        /* Interrupted trial: return to the previous settings before workers start. */
        next.reset_auth |= strcmp(next.active.tenant, next.candidate.tenant) != 0 ||
                           strcmp(next.active.client, next.candidate.client) != 0;
        next.pending = next.tried = 0;
    } else if (trial)
        next.tried = 1;
    if (next.reset_auth) {
        err = token_storage_write(NULL);
        if (err != ESP_OK)
            return err;
        next.reset_auth = 0;
    }
    if (memcmp(&record, &next, sizeof(next)) && (err = persist(&next)) != ESP_OK)
        return err;
    current = trial ? record.candidate : record.active;
    boot_at = esp_timer_get_time();
    reboot_at = 0;
    next_commit = 0;
    return ESP_OK;
}

const frame_settings_t *settings_get(void)
{
    return &current;
}

static bool string_field(const cJSON *json, const char *name, char *dest, size_t size)
{
    const char *s = json_string(json, name);
    if (!s || strlen(s) >= size)
        return false;
    strcpy(dest, s);
    return true;
}
static bool number_field(const cJSON *json, const char *name, int *value)
{
    const cJSON *n = cJSON_GetObjectItemCaseSensitive(json, name);
    if (!cJSON_IsNumber(n) || !isfinite(n->valuedouble) || n->valuedouble < 0 ||
        n->valuedouble > 3600 || floor(n->valuedouble) != n->valuedouble)
        return false;
    *value = (int)n->valuedouble;
    return true;
}
bool settings_parse(const cJSON *json, frame_settings_t *s)
{
    *s = current;
    if (!cJSON_IsObject(json) || !string_field(json, "ssid", s->ssid, sizeof(s->ssid)) ||
        !string_field(json, "tenant", s->tenant, sizeof(s->tenant)) ||
        !string_field(json, "client", s->client, sizeof(s->client)) ||
        !string_field(json, "ntp", s->ntp, sizeof(s->ntp)) ||
        !number_field(json, "poll_seconds", &s->poll_seconds) ||
        !number_field(json, "stale_seconds", &s->stale_seconds) ||
        !number_field(json, "brightness", &s->brightness))
        return false;
    const cJSON *open = cJSON_GetObjectItemCaseSensitive(json, "open_network");
    const cJSON *password = cJSON_GetObjectItemCaseSensitive(json, "password");
    if ((open && !cJSON_IsBool(open)) || (password && !cJSON_IsString(password)))
        return false;
    if (cJSON_IsTrue(open))
        s->password[0] = 0;
    else if (password && password->valuestring[0] &&
             !string_field(json, "password", s->password, sizeof(s->password)))
        return false;
    return settings_valid(s, true);
}
cJSON *settings_json(void)
{
    const frame_settings_t *s = &current;
    cJSON *j = cJSON_CreateObject();
    if (!j || !cJSON_AddStringToObject(j, "ssid", s->ssid) ||
        !cJSON_AddStringToObject(j, "tenant", s->tenant) ||
        !cJSON_AddStringToObject(j, "client", s->client) ||
        !cJSON_AddStringToObject(j, "ntp", s->ntp) ||
        !cJSON_AddNumberToObject(j, "poll_seconds", s->poll_seconds) ||
        !cJSON_AddNumberToObject(j, "stale_seconds", s->stale_seconds) ||
        !cJSON_AddNumberToObject(j, "brightness", s->brightness) ||
        !cJSON_AddBoolToObject(j, "password_set", s->password[0] != 0) ||
        !cJSON_AddBoolToObject(j, "trial", settings_trial())) {
        cJSON_Delete(j);
        return NULL;
    }
    return j;
}

esp_err_t settings_save(const frame_settings_t *s)
{
    if (!settings_valid(s, true))
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(mutex, portMAX_DELAY);
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!trial && !reboot_at) {
        settings_record_t next = record;
        next.candidate = *s;
        next.pending = 1;
        next.tried = 0;
        next.reset_auth = strcmp(s->tenant, current.tenant) || strcmp(s->client, current.client);
        err = persist(&next);
        if (err == ESP_OK)
            reboot_at = esp_timer_get_time() + 2000000;
    }
    xSemaphoreGive(mutex);
    return err;
}
esp_err_t settings_reset_auth(void)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!trial && !reboot_at) {
        settings_record_t next = record;
        next.reset_auth = 1;
        err = persist(&next);
        if (err == ESP_OK)
            reboot_at = esp_timer_get_time() + 2000000;
    }
    xSemaphoreGive(mutex);
    return err;
}
bool settings_trial(void)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool value = trial;
    xSemaphoreGive(mutex);
    return value;
}
bool settings_tick(bool online, int64_t now)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (trial && !reboot_at) {
        if (now - boot_at >= 180000000)
            reboot_at = now; /* Tried marker makes next boot roll back. */
        else if (online && now >= next_commit) {
            settings_record_t next = record;
            next.active = current;
            next.pending = next.tried = 0;
            if (persist(&next) == ESP_OK)
                trial = false;
            next_commit = now + 5000000;
        }
    }
    bool restart = reboot_at && now >= reboot_at;
    xSemaphoreGive(mutex);
    return restart;
}
