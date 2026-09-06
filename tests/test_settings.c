#include "../main/settings.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static settings_record_t disk;
static bool exists, held;
static int64_t clock_now;
static esp_err_t store_error, token_error;
static unsigned erased;
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
    strcpy(next.ssid, "new-network");
    store_error = ESP_FAIL;
    assert(settings_save(&next) == ESP_FAIL);
    assert(!settings_tick(false, 3000000));
    store_error = 0;
    assert(settings_save(&next) == ESP_OK);
    assert(settings_reset_auth() == ESP_ERR_INVALID_STATE);
    assert(!strcmp(settings_get()->ssid, original.ssid));
    assert(!settings_tick(false, 1999999) && settings_tick(false, 2000000));
    clock_now = 0;
    assert(settings_init() == ESP_OK && settings_trial());
    assert(!strcmp(settings_get()->ssid, "new-network"));
    assert(settings_save(&original) == ESP_ERR_INVALID_STATE);
    assert(!settings_tick(false, 179999999));
    assert(settings_tick(false, 180000000));
    assert(settings_init() == ESP_OK && !settings_trial());
    assert(!strcmp(settings_get()->ssid, original.ssid));
    assert(settings_save(&next) == ESP_OK);
    assert(settings_init() == ESP_OK);
    assert(!settings_tick(true, 1000000) && !settings_trial());
    assert(settings_init() == ESP_OK && !strcmp(settings_get()->ssid, "new-network"));
    next = *settings_get();
    next.brightness = 20;
    assert(settings_save(&next) == ESP_OK);
    assert(settings_init() == ESP_OK);
    /* An interrupted trial rolls back even before its timeout. */
    assert(settings_init() == ESP_OK && settings_get()->brightness == 100);
    assert(erased == 0);
    assert(settings_reset_auth() == ESP_OK && erased == 0);
    token_error = ESP_FAIL;
    assert(settings_init() == ESP_FAIL && disk.reset_auth);
    token_error = 0;
    assert(settings_init() == ESP_OK && !disk.reset_auth && erased == 2);
    next = *settings_get();
    strcpy(next.client, "33333333-3333-3333-3333-333333333333");
    assert(settings_save(&next) == ESP_OK);
    assert(settings_init() == ESP_OK && erased == 3);
    assert(settings_init() == ESP_OK && erased == 4); /* rollback restores old identity safely */
    disk.version = 999;
    assert(settings_init() == ESP_OK && !settings_trial());
    assert(!strcmp(settings_get()->ssid, original.ssid));
    puts("Settings validation, redaction, storage errors, trial rollback/promotion and "
         "authorization reset passed");
}
