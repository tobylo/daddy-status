#include "storage.h"
#include "esp_flash_encrypt.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "mbedtls/platform_util.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <string.h>

#if CONFIG_NVS_ENCRYPTION && !CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC
#error "ESP32 encrypted storage requires flash-protected NVS keys"
#endif

#if CONFIG_NVS_ENCRYPTION
static esp_err_t require_empty_nvs(void)
{
    const esp_partition_t *nvs =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs");
    if (!nvs)
        return ESP_ERR_NOT_FOUND;
    unsigned char data[256], erased[256];
    memset(erased, 0xff, sizeof(erased));
    for (size_t offset = 0; offset < nvs->size; offset += sizeof(data)) {
        size_t length = nvs->size - offset;
        if (length > sizeof(data))
            length = sizeof(data);
        esp_err_t err = esp_partition_read_raw(nvs, offset, data, length);
        if (err != ESP_OK)
            return err;
        if (memcmp(data, erased, length))
            return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static esp_err_t load_encryption_keys(nvs_sec_cfg_t *keys)
{
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS, NULL);
    if (!partition)
        return ESP_ERR_NOT_FOUND;
    if (!partition->encrypted)
        return ESP_ERR_INVALID_STATE;
    esp_err_t err = nvs_flash_read_security_cfg(partition, keys);
    if (err != ESP_ERR_NVS_KEYS_NOT_INITIALIZED)
        return err;
    /* Missing keys with existing data need deliberate recovery, not new keys. */
    err = require_empty_nvs();
    return err == ESP_OK ? nvs_flash_generate_keys(partition, keys) : err;
}

static esp_err_t encrypted_storage_init(void)
{
    nvs_sec_cfg_t keys = {0};
    esp_err_t err = load_encryption_keys(&keys);
    if (err == ESP_OK)
        err = nvs_flash_secure_init(&keys);
    mbedtls_platform_zeroize(&keys, sizeof(keys));
    return err;
}
#endif

esp_err_t storage_init(void)
{
#if CONFIG_NVS_ENCRYPTION
    if (!esp_flash_encryption_enabled()) {
        ESP_LOGE("storage", "Encrypted NVS requires provisioned flash encryption");
        return ESP_ERR_INVALID_STATE;
    }
    return encrypted_storage_init();
#else
    if (esp_flash_encryption_enabled()) {
        ESP_LOGE("storage", "Encrypted device requires firmware with NVS encryption");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = nvs_flash_init();
    if (err != ESP_ERR_NVS_NO_FREE_PAGES && err != ESP_ERR_NVS_NEW_VERSION_FOUND)
        return err;
    ESP_LOGW("storage", "NVS requires reinitialization; saved authorization will be erased");
    err = nvs_flash_erase();
    return err == ESP_OK ? nvs_flash_init() : err;
#endif
}
