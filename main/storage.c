#include "storage.h"
#include "esp_flash_encrypt.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#if CONFIG_NVS_ENCRYPTION && !CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC
#error "ESP32 encrypted storage requires flash-protected NVS keys"
#endif

esp_err_t storage_init(void)
{
#if CONFIG_NVS_ENCRYPTION
    if (!esp_flash_encryption_enabled()) {
        ESP_LOGE("storage", "Encrypted NVS requires provisioned flash encryption");
        return ESP_ERR_INVALID_STATE;
    }
    /* IDF generates/loads protected keys. Never erase on encryption errors. */
    return nvs_flash_init();
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
