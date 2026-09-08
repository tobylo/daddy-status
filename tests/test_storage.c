#include "esp_flash_encrypt.h"
#include "nvs_flash.h"
#include "storage.h"
#include <assert.h>
#include <stdio.h>

static bool encrypted;
static unsigned init_calls, erase_calls;
static esp_err_t first_result, retry_result, erase_result;

bool esp_flash_encryption_enabled(void)
{
    return encrypted;
}

esp_err_t nvs_flash_init(void)
{
    return ++init_calls == 1 ? first_result : retry_result;
}

esp_err_t nvs_flash_erase(void)
{
    ++erase_calls;
    return erase_result;
}

static void prepare(bool flash_encrypted, esp_err_t result)
{
    encrypted = flash_encrypted;
    first_result = result;
    retry_result = erase_result = ESP_OK;
    init_calls = erase_calls = 0;
}

static void rejects_mismatched_firmware(void)
{
    prepare(!CONFIG_NVS_ENCRYPTION, ESP_OK);
    assert(storage_init() == ESP_ERR_INVALID_STATE);
    assert(init_calls == 0 && erase_calls == 0);
}

static void initializes_storage(void)
{
    prepare(CONFIG_NVS_ENCRYPTION, ESP_OK);
    assert(storage_init() == ESP_OK);
    assert(init_calls == 1 && erase_calls == 0);
}

static void preserves_security_failures(void)
{
    const esp_err_t errors[] = {ESP_FAIL, ESP_ERR_NVS_CORRUPT_KEY_PART,
                                ESP_ERR_NVS_WRONG_ENCRYPTION, ESP_ERR_NOT_FOUND};
    for (unsigned i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        prepare(CONFIG_NVS_ENCRYPTION, errors[i]);
        assert(storage_init() == errors[i]);
        assert(init_calls == 1 && erase_calls == 0);
    }
}

static void handles_reinitialization(void)
{
    const esp_err_t errors[] = {ESP_ERR_NVS_NO_FREE_PAGES, ESP_ERR_NVS_NEW_VERSION_FOUND};
    for (unsigned i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        prepare(CONFIG_NVS_ENCRYPTION, errors[i]);
#if CONFIG_NVS_ENCRYPTION
        assert(storage_init() == errors[i]);
        assert(init_calls == 1 && erase_calls == 0);
#else
        assert(storage_init() == ESP_OK);
        assert(init_calls == 2 && erase_calls == 1);
#endif
    }
}

#if !CONFIG_NVS_ENCRYPTION
static void propagates_recovery_failures(void)
{
    prepare(false, ESP_ERR_NVS_NO_FREE_PAGES);
    erase_result = ESP_FAIL;
    assert(storage_init() == ESP_FAIL);
    assert(init_calls == 1 && erase_calls == 1);
    prepare(false, ESP_ERR_NVS_NEW_VERSION_FOUND);
    retry_result = ESP_ERR_NO_MEM;
    assert(storage_init() == ESP_ERR_NO_MEM);
    assert(init_calls == 2 && erase_calls == 1);
}
#endif

int main(void)
{
    rejects_mismatched_firmware();
    initializes_storage();
    preserves_security_failures();
    handles_reinitialization();
#if !CONFIG_NVS_ENCRYPTION
    propagates_recovery_failures();
#endif
    puts("Storage encryption gate, initialization and error recovery tests passed");
    return 0;
}
