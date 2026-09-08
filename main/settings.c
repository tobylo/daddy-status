#include "settings.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ledcontrol.h"
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
static bool trial, firmware_update;
static int brightness;
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

static bool password_character_valid(unsigned char c, bool hex)
{
    return hex ? isxdigit(c) != 0 : c >= 32 && c <= 126;
}

static bool password_valid(const char *password)
{
    size_t n = strlen(password);
    if (!n)
        return true;
    if (n < 8 || n > 64)
        return false;
    for (size_t i = 0; i < n; ++i)
        if (!password_character_valid((unsigned char)password[i], n == 64))
            return false;
    return true;
}

static bool ntp_host_valid(const char *host)
{
    if (!*host)
        return false;
    for (const unsigned char *p = (const unsigned char *)host; *p; ++p)
        if (!(isalnum(*p) || *p == '.' || *p == '-'))
            return false;
    return true;
}

static bool in_range(int value, int minimum, int maximum)
{
    return value >= minimum && value <= maximum;
}

static const char *invalid_range(const frame_settings_t *s)
{
    if (!in_range(s->poll_seconds, 2, 300))
        return "poll_seconds";
    if (!in_range(s->stale_seconds, 30, 3600))
        return "stale_seconds";
    if (s->stale_seconds <= s->poll_seconds)
        return "stale_seconds";
    if (!in_range(s->brightness, 1, 100))
        return "brightness";
    return NULL;
}

static const char *unterminated_field(const frame_settings_t *s)
{
#define TERMINATED(field)                                                                          \
    if (!memchr(s->field, 0, sizeof(s->field)))                                                    \
    return #field
    TERMINATED(ssid);
    TERMINATED(password);
    TERMINATED(tenant);
    TERMINATED(client);
    TERMINATED(ntp);
#undef TERMINATED
    return NULL;
}

static bool identity_valid(const char *value, bool complete)
{
    return !complete && !*value ? true : guid_valid(value);
}

static const char *invalid_connection(const frame_settings_t *s, bool complete)
{
    if (!password_valid(s->password))
        return "password";
    if (complete && !s->ssid[0])
        return "ssid";
    if (!identity_valid(s->tenant, complete))
        return "tenant";
    if (!identity_valid(s->client, complete))
        return "client";
    if (!ntp_host_valid(s->ntp))
        return "ntp";
    return NULL;
}

static const char *invalid_field(const frame_settings_t *s, bool complete)
{
    if (!s)
        return "request";
    const char *error = unterminated_field(s);
    if (error)
        return error;
    error = invalid_connection(s, complete);
    return error ? error : invalid_range(s);
}
bool settings_valid(const frame_settings_t *s, bool complete)
{
    return invalid_field(s, complete) == NULL;
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

static bool record_valid(const settings_record_t *saved)
{
    if (saved->version != 1)
        return false;
    if (saved->pending > 1 || saved->tried > 1)
        return false;
    if (saved->reset_auth > 1)
        return false;
    if (!settings_valid(&saved->active, false))
        return false;
    return !saved->pending || settings_valid(&saved->candidate, true);
}

static esp_err_t load_saved_record(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("frame", NVS_READONLY, &h);
    if (err != ESP_OK)
        return err;
    settings_record_t saved = record;
    size_t size = 0;
    err = nvs_get_blob(h, "settings", NULL, &size);
    if (err == ESP_OK && size == sizeof(saved))
        err = nvs_get_blob(h, "settings", &saved, &size);
    nvs_close(h);
    if (err == ESP_ERR_NVS_TYPE_MISMATCH) {
        ESP_LOGW("settings", "Invalid settings storage type; using compiled defaults");
        return ESP_OK;
    }
    if (err != ESP_OK)
        return err;
    if (size == sizeof(saved) && record_valid(&saved))
        record = saved;
    else
        ESP_LOGW("settings", "Invalid saved settings; using compiled defaults for recovery");
    return ESP_OK;
}

static bool identity_changed(const frame_settings_t *a, const frame_settings_t *b)
{
    return strcmp(a->tenant, b->tenant) || strcmp(a->client, b->client);
}

static esp_err_t resolve_interrupted_trial(settings_record_t *next)
{
    trial = next->pending && !next->tried;
    if (next->pending && next->tried) {
        /* Interrupted trial: return to the previous settings before workers start. */
        next->reset_auth |= identity_changed(&next->active, &next->candidate);
        next->pending = next->tried = 0;
    } else if (trial)
        next->tried = 1;
    if (!next->reset_auth)
        return ESP_OK;
    esp_err_t err = token_storage_write(NULL);
    if (err == ESP_OK)
        next->reset_auth = 0;
    return err;
}

esp_err_t settings_init(void)
{
    mutex = xSemaphoreCreateMutex();
    if (!mutex)
        return ESP_ERR_NO_MEM;
    record = (settings_record_t){.version = 1};
    settings_defaults(&record.active);
    esp_err_t err = load_saved_record();
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        return err;
    settings_record_t next = record;
    err = resolve_interrupted_trial(&next);
    if (err != ESP_OK)
        return err;
    if (memcmp(&record, &next, sizeof(next))) {
        err = persist(&next);
        if (err != ESP_OK)
            return err;
    }
    current = trial ? record.candidate : record.active;
    brightness = current.brightness;
    boot_at = esp_timer_get_time();
    reboot_at = 0;
    next_commit = 0;
    return ESP_OK;
}

const frame_settings_t *settings_get(void)
{
    return &current;
}

int settings_brightness(void)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    int value = brightness;
    xSemaphoreGive(mutex);
    return value;
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
    if (!cJSON_IsNumber(n) || !isfinite(n->valuedouble))
        return false;
    if (n->valuedouble < 0 || n->valuedouble > 3600)
        return false;
    if (floor(n->valuedouble) != n->valuedouble)
        return false;
    *value = (int)n->valuedouble;
    return true;
}
static const char *parse_required_fields(const cJSON *json, frame_settings_t *s)
{
#define STRING(field)                                                                              \
    if (!string_field(json, #field, s->field, sizeof(s->field)))                                   \
    return #field
    STRING(ssid);
    STRING(tenant);
    STRING(client);
    STRING(ntp);
#undef STRING
#define NUMBER(field)                                                                              \
    if (!number_field(json, #field, &s->field))                                                    \
    return #field
    NUMBER(poll_seconds);
    NUMBER(stale_seconds);
    NUMBER(brightness);
#undef NUMBER
    return NULL;
}

static bool password_supplied(const cJSON *password)
{
    return password && password->valuestring[0];
}

static const char *parse_password(const cJSON *json, frame_settings_t *s)
{
    const cJSON *open = cJSON_GetObjectItemCaseSensitive(json, "open_network");
    const cJSON *password = cJSON_GetObjectItemCaseSensitive(json, "password");
    if (open && !cJSON_IsBool(open))
        return "open";
    if (password && !cJSON_IsString(password))
        return "password";
    if (cJSON_IsTrue(open))
        s->password[0] = 0;
    else if (password_supplied(password) &&
             !string_field(json, "password", s->password, sizeof(s->password)))
        return "password";
    return NULL;
}

const char *settings_parse_error(const cJSON *json, frame_settings_t *s)
{
    *s = current;
    s->brightness = settings_brightness();
    if (!cJSON_IsObject(json))
        return "request";
    const char *error = parse_required_fields(json, s);
    if (error)
        return error;
    error = parse_password(json, s);
    return error ? error : invalid_field(s, true);
}
bool settings_parse(const cJSON *json, frame_settings_t *s)
{
    return settings_parse_error(json, s) == NULL;
}
static bool add_settings_fields(cJSON *j, const frame_settings_t *s)
{
    return cJSON_AddStringToObject(j, "ssid", s->ssid) &&
           cJSON_AddStringToObject(j, "tenant", s->tenant) &&
           cJSON_AddStringToObject(j, "client", s->client) &&
           cJSON_AddStringToObject(j, "ntp", s->ntp) &&
           cJSON_AddNumberToObject(j, "poll_seconds", s->poll_seconds) &&
           cJSON_AddNumberToObject(j, "stale_seconds", s->stale_seconds) &&
           cJSON_AddNumberToObject(j, "brightness", settings_brightness());
}

typedef struct {
    bool trying, restarting;
    int64_t remaining;
} settings_status_t;

static settings_status_t settings_status(void)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool trying = trial, restarting = reboot_at != 0;
    int64_t remaining = boot_at + 180000000 - esp_timer_get_time();
    xSemaphoreGive(mutex);
    return (settings_status_t){trying, restarting, remaining};
}

static bool add_settings_status(cJSON *j, settings_status_t status)
{
    int64_t seconds =
        status.trying && status.remaining > 0 ? (status.remaining + 999999) / 1000000 : 0;
    return cJSON_AddBoolToObject(j, "password_set", current.password[0] != 0) &&
           cJSON_AddBoolToObject(j, "trial", status.trying) &&
           cJSON_AddBoolToObject(j, "restart_pending", status.restarting) &&
           cJSON_AddNumberToObject(j, "trial_seconds_remaining", seconds);
}

cJSON *settings_json(void)
{
    settings_status_t status = settings_status();
    cJSON *j = cJSON_CreateObject();
    if (!j)
        return NULL;
    if (!add_settings_fields(j, &current) || !add_settings_status(j, status)) {
        cJSON_Delete(j);
        return NULL;
    }
    return j;
}

/* Compare values, not padding or unused bytes after string terminators. */
static bool wifi_changed(const frame_settings_t *a, const frame_settings_t *b)
{
    return strcmp(a->ssid, b->ssid) || strcmp(a->password, b->password);
}

static bool settings_busy(void)
{
    return trial || reboot_at || firmware_update;
}

static bool restart_required(const frame_settings_t *s)
{
    return wifi_changed(s, &current) || identity_changed(s, &current) ||
           strcmp(s->ntp, current.ntp) || s->poll_seconds != current.poll_seconds ||
           s->stale_seconds != current.stale_seconds;
}

static settings_save_result_t save_result(bool wifi, bool restart)
{
    if (wifi)
        return SETTINGS_WIFI_TRIAL;
    return restart ? SETTINGS_RESTART : SETTINGS_APPLIED;
}

/* Caller holds the mutex; publish live changes only after persistence succeeds. */
static esp_err_t save_settings(const frame_settings_t *s, settings_save_result_t *result,
                               bool *refresh)
{
    if (settings_busy())
        return ESP_ERR_INVALID_STATE;
    bool restart = restart_required(s);
    if (!restart && s->brightness == brightness) {
        *result = SETTINGS_UNCHANGED;
        return ESP_OK;
    }
    bool wifi = wifi_changed(s, &current);
    settings_record_t next = record;
    if (wifi)
        next.candidate = *s;
    else
        next.active = *s;
    next.pending = wifi;
    next.tried = 0;
    next.reset_auth = identity_changed(s, &current);
    esp_err_t err = persist(&next);
    if (err != ESP_OK)
        return err;
    *refresh = brightness != s->brightness;
    brightness = s->brightness;
    *result = save_result(wifi, restart);
    if (restart)
        reboot_at = esp_timer_get_time() + 2000000;
    return ESP_OK;
}

esp_err_t settings_save(const frame_settings_t *s, settings_save_result_t *result)
{
    if (!result || !settings_valid(s, true))
        return ESP_ERR_INVALID_ARG;
    bool refresh = false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    esp_err_t err = save_settings(s, result, &refresh);
    xSemaphoreGive(mutex);
    if (refresh)
        leds_refresh();
    return err;
}
esp_err_t settings_reset_auth(void)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!settings_busy()) {
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
static void advance_trial(bool online, int64_t now)
{
    if (now - boot_at >= 180000000) {
        reboot_at = now; /* Tried marker makes next boot roll back. */
        return;
    }
    if (!online || now < next_commit)
        return;
    settings_record_t next = record;
    next.active = current;
    next.pending = next.tried = 0;
    if (persist(&next) == ESP_OK)
        trial = false;
    next_commit = now + 5000000;
}

bool settings_tick(bool online, int64_t now)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (trial && !reboot_at)
        advance_trial(online, now);
    bool restart = reboot_at && now >= reboot_at;
    xSemaphoreGive(mutex);
    return restart;
}

bool settings_update_begin(void)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool allowed = !settings_busy();
    if (allowed)
        firmware_update = true;
    xSemaphoreGive(mutex);
    return allowed;
}

void settings_update_end(void)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    firmware_update = false;
    xSemaphoreGive(mutex);
}
