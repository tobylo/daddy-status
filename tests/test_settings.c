#include "../main/settings.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static settings_record_t disk;
static bool exists, held;
static int64_t clock_now;
static esp_err_t store_error, token_error;
static unsigned erased, writes, refreshes;
void leds_refresh(void)
{
    assert(!held);
    assert(settings_brightness() == disk.active.brightness ||
           settings_brightness() == disk.candidate.brightness);
    ++refreshes;
}
static settings_save_result_t result;
SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    return (void *)1;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t t)
{
    assert(!held);
    held = true;
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t s)
{
    assert(held);
    held = false;
    return pdTRUE;
}
int64_t esp_timer_get_time(void)
{
    return clock_now;
}
esp_err_t token_storage_write(const char *token)
{
    assert(!token);
    ++erased;
    return token_error;
}
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *h)
{
    assert(!strcmp(name, "frame"));
    *h = 1;
    return ESP_OK;
}
void nvs_close(nvs_handle_t h)
{
    assert(h == 1);
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *n)
{
    assert(!strcmp(key, "settings"));
    if (!exists)
        return ESP_ERR_NVS_NOT_FOUND;
    if (out) {
        assert(*n == sizeof(disk));
        memcpy(out, &disk, sizeof(disk));
    }
    *n = sizeof(disk);
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *in, size_t n)
{
    ++writes;
    assert(n == sizeof(disk));
    if (store_error)
        return store_error;
    memcpy(&disk, in, n);
    exists = true;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h)
{
    return ESP_OK;
}
int main(void)
{
    assert(settings_init() == ESP_OK);
    frame_settings_t original = *settings_get(), next = original;
    assert(settings_valid(&next, true));
    next.poll_seconds = 60;
    assert(!settings_valid(&next, true));
    next = original;
    next.brightness = 0;
    assert(!settings_valid(&next, true));
    next = original;
    strcpy(next.ntp, "https://bad/path");
    assert(!settings_valid(&next, true));
    next = original;
    memset(next.ssid, 'x', sizeof(next.ssid));
    assert(!settings_valid(&next, true));
    next = original;
    strcpy(next.password, "short");
    assert(!settings_valid(&next, true));
    next = original;
    strcpy(next.password, "eight\nchars");
    assert(!settings_valid(&next, true));
    strcpy(next.password, "eight\177chars");
    assert(!settings_valid(&next, true));
    strcpy(next.password, "eight\200chars");
    assert(!settings_valid(&next, true));
    strcpy(next.password, "spaces are valid");
    assert(settings_valid(&next, true));
    memset(next.password, 'a', 64);
    next.password[64] = 0;
    assert(settings_valid(&next, true));
    next.password[63] = 'z';
    assert(!settings_valid(&next, true));
    next = original;
    cJSON *j = settings_json();
    assert(j && !cJSON_GetObjectItem(j, "password"));
    char *text = cJSON_PrintUnformatted(j);
    assert(!strstr(text, original.password));
    free(text);
    assert(settings_parse(j, &next) && !strcmp(next.password, original.password));
    cJSON_AddBoolToObject(j, "open_network", true);
    assert(settings_parse(j, &next) && !next.password[0]);
    cJSON_ReplaceItemInObject(j, "poll_seconds", cJSON_CreateNumber(2.5));
    assert(!settings_parse(j, &next));
    cJSON_Delete(j);
    next = original;
    /* Trailing bytes are not changes; no-op saves never touch NVS. */
    next.ssid[sizeof(next.ssid) - 1] = 'x';
    assert(settings_save(&next, &result) == ESP_OK && result == SETTINGS_UNCHANGED);
    assert(writes == 0 && refreshes == 0 && !settings_tick(false, 3000000));
    next = original;
    next.brightness = 20;
    store_error = ESP_FAIL;
    assert(settings_save(&next, &result) == ESP_FAIL);
    assert(settings_brightness() == original.brightness && refreshes == 0);
    store_error = 0;
    assert(settings_save(&next, &result) == ESP_OK && result == SETTINGS_APPLIED);
    assert(settings_brightness() == 20 && refreshes == 1 && !settings_tick(false, 3000000));
    unsigned saved_writes = writes;
    assert(settings_save(&next, &result) == ESP_OK && result == SETTINGS_UNCHANGED);
    assert(writes == saved_writes && refreshes == 1);
    j = settings_json();
    assert(cJSON_GetObjectItem(j, "brightness")->valueint == 20);
    cJSON_Delete(j);
    assert(settings_init() == ESP_OK && !settings_trial() && settings_brightness() == 20);
    assert(settings_save(&original, &result) == ESP_OK && result == SETTINGS_APPLIED);
    assert(settings_init() == ESP_OK);
    next = original;
    strcpy(next.ssid, "new-network");
    store_error = ESP_FAIL;
    assert(settings_save(&next, &result) == ESP_FAIL);
    assert(!settings_tick(false, 3000000));
    store_error = 0;
    assert(settings_save(&next, &result) == ESP_OK);
    assert(settings_reset_auth() == ESP_ERR_INVALID_STATE);
    assert(!strcmp(settings_get()->ssid, original.ssid));
    assert(!settings_tick(false, 1999999) && settings_tick(false, 2000000));
    clock_now = 0;
    assert(settings_init() == ESP_OK && settings_trial());
    assert(!strcmp(settings_get()->ssid, "new-network"));
    assert(settings_save(&original, &result) == ESP_ERR_INVALID_STATE);
    assert(!settings_tick(false, 179999999));
    assert(settings_tick(false, 180000000));
    assert(settings_init() == ESP_OK && !settings_trial());
    assert(!strcmp(settings_get()->ssid, original.ssid));
    assert(settings_save(&next, &result) == ESP_OK);
    assert(settings_init() == ESP_OK);
    assert(!settings_tick(true, 1000000) && !settings_trial());
    assert(settings_init() == ESP_OK && !strcmp(settings_get()->ssid, "new-network"));
    next = *settings_get();
    next.brightness = 20;
    assert(settings_save(&next, &result) == ESP_OK);
    assert(settings_init() == ESP_OK);
    assert(!settings_trial() && settings_brightness() == 20);
    assert(settings_init() == ESP_OK && settings_brightness() == 20);
    assert(erased == 0);
    assert(settings_reset_auth() == ESP_OK && erased == 0);
    token_error = ESP_FAIL;
    assert(settings_init() == ESP_FAIL && disk.reset_auth);
    token_error = 0;
    assert(settings_init() == ESP_OK && !disk.reset_auth && erased == 2);
    next = *settings_get();
    strcpy(next.client, "33333333-3333-3333-3333-333333333333");
    assert(settings_save(&next, &result) == ESP_OK);
    assert(settings_init() == ESP_OK && erased == 3);
    assert(!settings_trial() && result == SETTINGS_RESTART);
    assert(settings_init() == ESP_OK && erased == 3);
    assert(!strcmp(settings_get()->client, next.client));
    next.ntp[0] = 'a';
    next.poll_seconds = 15;
    next.stale_seconds = 90;
    assert(settings_save(&next, &result) == ESP_OK && result == SETTINGS_RESTART);
    assert(settings_tick(false, 2000000));
    assert(settings_init() == ESP_OK && !settings_trial());
    assert(settings_get()->poll_seconds == 15 && settings_get()->stale_seconds == 90);
    /* Password-only changes also trial; mixed identity changes reset on rollback. */
    strcpy(next.password, "new-password");
    strcpy(next.client, original.client);
    next.brightness = 30;
    assert(settings_save(&next, &result) == ESP_OK && result == SETTINGS_WIFI_TRIAL);
    assert(settings_brightness() == 30);
    assert(settings_init() == ESP_OK && settings_trial() && erased == 4);
    assert(settings_init() == ESP_OK && !settings_trial() && erased == 5);
    assert(settings_brightness() == 20);
    assert(strcmp(settings_get()->client, original.client));
    disk.version = 999;
    assert(settings_init() == ESP_OK && !settings_trial());
    assert(!strcmp(settings_get()->ssid, original.ssid));
    puts("Settings validation, redaction, storage errors, trial rollback/promotion and "
         "authorization reset passed");
}
