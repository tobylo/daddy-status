#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "firmware_web.h"
#include "sdkconfig.h"
#include "settings.h"

#if CONFIG_FRAME_OTA_ENABLE
static bool settings_unavailable(const cJSON *settings)
{
    return !settings || cJSON_IsTrue(cJSON_GetObjectItem(settings, "trial")) ||
           cJSON_IsTrue(cJSON_GetObjectItem(settings, "restart_pending"));
}

#endif

static const char *update_readiness(const esp_partition_t *target, const esp_partition_t *running,
                                    bool probation)
{
#if !CONFIG_FRAME_OTA_ENABLE
    return "disabled";
#else
    if (!target || target == running)
        return "unavailable";
    if (probation)
        return "probation";
    cJSON *settings = settings_json();
    bool busy = settings_unavailable(settings);
    cJSON_Delete(settings);
    return busy ? "busy" : "ready";
#endif
}

static bool add_slot_fields(cJSON *json, const esp_partition_t *running,
                            const esp_partition_t *target)
{
    return cJSON_AddNumberToObject(json, "partition", running ? running->address : 0) &&
           cJSON_AddNumberToObject(json, "max_size", target ? target->size : 0);
}

cJSON *firmware_status_json(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    esp_ota_get_state_partition(running, &state);
    cJSON *json = cJSON_CreateObject();
    bool complete = json &&
                    cJSON_AddStringToObject(json, "version", esp_app_get_description()->version) &&
                    add_slot_fields(json, running, target) &&
                    cJSON_AddBoolToObject(json, "confirmed", state == ESP_OTA_IMG_VALID) &&
                    cJSON_AddStringToObject(
                        json, "readiness",
                        update_readiness(target, running, state == ESP_OTA_IMG_PENDING_VERIFY));
    if (!complete) {
        cJSON_Delete(json);
        return NULL;
    }
    return json;
}
