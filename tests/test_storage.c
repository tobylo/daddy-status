#include "esp_flash_encrypt.h"
#include "mbedtls/platform_util.h"
#include "nvs_flash.h"
#include "storage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool encrypted;
static unsigned init_calls, erase_calls;
static esp_err_t first_result, retry_result, erase_result;
static esp_partition_t key_partition = {.size = 4096, .encrypted = true};
static const esp_partition_t data_partition = {.size = 769};
static unsigned char nvs_data[769];
static bool key_partition_present, data_partition_present;
static esp_err_t key_read_result, key_generate_result, raw_read_result;
static unsigned generate_calls, plain_calls, zero_calls;

const esp_partition_t *esp_partition_find_first(esp_partition_type_t type,
                                                esp_partition_subtype_t subtype, const char *label)
{
    assert(type == ESP_PARTITION_TYPE_DATA);
    if (subtype == ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS) {
        assert(label == NULL);
        return key_partition_present ? &key_partition : NULL;
    }
    assert(subtype == ESP_PARTITION_SUBTYPE_DATA_NVS && !strcmp(label, "nvs"));
    return data_partition_present ? &data_partition : NULL;
}

esp_err_t esp_partition_read_raw(const esp_partition_t *partition, size_t offset, void *data,
                                 size_t length)
{
    assert(partition == &data_partition && offset + length <= sizeof(nvs_data));
    memcpy(data, nvs_data + offset, length);
    return raw_read_result;
}

esp_err_t nvs_flash_read_security_cfg(const esp_partition_t *partition, nvs_sec_cfg_t *keys)
{
    assert(partition == &key_partition);
    memset(keys, 0xa5, sizeof(*keys));
    return key_read_result;
}

esp_err_t nvs_flash_generate_keys(const esp_partition_t *partition, nvs_sec_cfg_t *keys)
{
    assert(partition == &key_partition);
    ++generate_calls;
    memset(keys, 0xa5, sizeof(*keys));
    return key_generate_result;
}

esp_err_t nvs_flash_secure_init(nvs_sec_cfg_t *keys)
{
    assert(keys->eky[0] == 0xa5 && keys->tky[31] == 0xa5);
    ++init_calls;
    return first_result;
}

void mbedtls_platform_zeroize(void *buffer, size_t size)
{
    assert(size == sizeof(nvs_sec_cfg_t));
    memset(buffer, 0, size);
    ++zero_calls;
}

bool esp_flash_encryption_enabled(void)
{
    return encrypted;
}

esp_err_t nvs_flash_init(void)
{
    ++plain_calls;
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
    init_calls = erase_calls = generate_calls = plain_calls = zero_calls = 0;
    key_partition_present = data_partition_present = key_partition.encrypted = true;
    key_read_result = key_generate_result = raw_read_result = ESP_OK;
    memset(nvs_data, 0xff, sizeof(nvs_data));
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
    assert(generate_calls == 0);
    assert(plain_calls == !CONFIG_NVS_ENCRYPTION && zero_calls == CONFIG_NVS_ENCRYPTION);
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

#if CONFIG_NVS_ENCRYPTION
static void assert_key_failure(esp_err_t expected)
{
    assert(storage_init() == expected);
    assert(init_calls == 0 && erase_calls == 0 && generate_calls == 0);
    assert(plain_calls == 0 && zero_calls == 1);
}

static void rejects_invalid_key_partitions(void)
{
    prepare(true, ESP_OK);
    key_partition_present = false;
    assert_key_failure(ESP_ERR_NOT_FOUND);
    prepare(true, ESP_OK);
    key_partition.encrypted = false;
    assert_key_failure(ESP_ERR_INVALID_STATE);
}

static void preserves_unreadable_keys(void)
{
    const esp_err_t errors[] = {ESP_FAIL, ESP_ERR_NVS_CORRUPT_KEY_PART, ESP_ERR_NO_MEM};
    for (unsigned i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        prepare(true, ESP_OK);
        key_read_result = errors[i];
        assert_key_failure(errors[i]);
    }
}

static void creates_keys_for_empty_storage(void)
{
    prepare(true, ESP_OK);
    key_read_result = ESP_ERR_NVS_KEYS_NOT_INITIALIZED;
    assert(storage_init() == ESP_OK);
    assert(generate_calls == 1 && init_calls == 1 && erase_calls == 0);
    assert(plain_calls == 0 && zero_calls == 1);
}

static void rejects_missing_keys_with_existing_data(void)
{
    const size_t positions[] = {0, 256, sizeof(nvs_data) - 1};
    for (unsigned i = 0; i < sizeof(positions) / sizeof(positions[0]); ++i) {
        prepare(true, ESP_OK);
        key_read_result = ESP_ERR_NVS_KEYS_NOT_INITIALIZED;
        nvs_data[positions[i]] = 0x42;
        assert_key_failure(ESP_ERR_INVALID_STATE);
        assert(nvs_data[positions[i]] == 0x42);
    }
}

static void requires_readable_empty_storage(void)
{
    prepare(true, ESP_OK);
    key_read_result = ESP_ERR_NVS_KEYS_NOT_INITIALIZED;
    data_partition_present = false;
    assert_key_failure(ESP_ERR_NOT_FOUND);
    prepare(true, ESP_OK);
    key_read_result = ESP_ERR_NVS_KEYS_NOT_INITIALIZED;
    raw_read_result = ESP_FAIL;
    assert_key_failure(ESP_FAIL);
}

static void propagates_key_generation_failure(void)
{
    prepare(true, ESP_OK);
    key_read_result = ESP_ERR_NVS_KEYS_NOT_INITIALIZED;
    key_generate_result = ESP_FAIL;
    assert(storage_init() == ESP_FAIL);
    assert(generate_calls == 1 && init_calls == 0 && erase_calls == 0);
    assert(plain_calls == 0 && zero_calls == 1);
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
#if CONFIG_NVS_ENCRYPTION
    rejects_invalid_key_partitions();
    preserves_unreadable_keys();
    creates_keys_for_empty_storage();
    rejects_missing_keys_with_existing_data();
    requires_readable_empty_storage();
    propagates_key_generation_failure();
#endif
    puts("Storage encryption gate, key protection and error recovery tests passed");
    return 0;
}
