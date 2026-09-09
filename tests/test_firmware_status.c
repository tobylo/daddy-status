#include "../main/firmware_status.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static esp_partition_t running = {.size = 8192, .address = 0x10000};
static esp_partition_t target = {.size = 8192, .address = 0x200000};
static const esp_partition_t *next_slot = &target;
static esp_ota_img_states_t boot_state = ESP_OTA_IMG_VALID;
static bool trial, restarting, fail_settings;
static esp_app_desc_t description = {.version = "v1.2.3"};

const esp_partition_t *esp_ota_get_running_partition(void)
{
    return &running;
}
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *p)
{
    return next_slot;
}
esp_err_t esp_ota_get_state_partition(const esp_partition_t *p, esp_ota_img_states_t *s)
{
    *s = boot_state;
    return ESP_OK;
}
const esp_app_desc_t *esp_app_get_description(void)
{
    return &description;
}
cJSON *settings_json(void)
{
    if (fail_settings)
        return NULL;
    cJSON *json = cJSON_CreateObject();
    bool complete = json && cJSON_AddBoolToObject(json, "trial", trial) &&
                    cJSON_AddBoolToObject(json, "restart_pending", restarting);
    if (!complete) {
        cJSON_Delete(json);
        return NULL;
    }
    return json;
}

static void check_readiness(const char *expected)
{
    cJSON *json = firmware_status_json();
    assert(json);
    assert(!strcmp(cJSON_GetObjectItem(json, "readiness")->valuestring, expected));
    assert(!strcmp(cJSON_GetObjectItem(json, "version")->valuestring, "v1.2.3"));
    assert(cJSON_GetObjectItem(json, "partition")->valueint == 0x10000);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(json, "confirmed")) ==
           (boot_state == ESP_OTA_IMG_VALID));
    cJSON_Delete(json);
}

static void readiness_states(void)
{
#if CONFIG_FRAME_OTA_ENABLE
    check_readiness("ready");
    trial = true;
    check_readiness("busy");
    trial = false;
    restarting = true;
    check_readiness("busy");
    restarting = false;
    fail_settings = true;
    check_readiness("busy");
    fail_settings = false;
    boot_state = ESP_OTA_IMG_PENDING_VERIFY;
    check_readiness("probation");
    boot_state = ESP_OTA_IMG_VALID;
    next_slot = NULL;
    check_readiness("unavailable");
    next_slot = &running;
    check_readiness("unavailable");
    next_slot = &target;
#else
    check_readiness("disabled");
#endif
}

static size_t allocations, fail_at;
static void *allocate(size_t size)
{
    return ++allocations == fail_at ? NULL : malloc(size);
}
static void allocation_failures(void)
{
    cJSON_Hooks hooks = {.malloc_fn = allocate, .free_fn = free};
    cJSON_InitHooks(&hooks);
    cJSON *json = firmware_status_json();
    assert(json);
    size_t total = allocations;
    cJSON_Delete(json);
    for (fail_at = 1; fail_at <= total; ++fail_at) {
        allocations = 0;
        json = firmware_status_json();
        // Failed readiness snapshots must fail closed, never advertise ready.
        if (json)
            assert(!strcmp(cJSON_GetObjectItem(json, "readiness")->valuestring, "busy"));
        cJSON_Delete(json);
    }
    cJSON_InitHooks(NULL);
}

int main(void)
{
    readiness_states();
    allocation_failures();
    puts("Firmware version, slots, readiness, boot confirmation and allocation failures passed");
}
