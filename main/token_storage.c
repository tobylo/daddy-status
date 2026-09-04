#include "token_storage.h"
#include "nvs.h"
#include "protocol.h"
#include <stdlib.h>
#include <string.h>

esp_err_t token_storage_write(const char *token)
{
    if (token && (!*token || strlen(token) > TOKEN_LIMIT))
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("graphapi", NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;
    err = token ? nvs_set_str(handle, "refresh_token", token)
                : nvs_erase_key(handle, "refresh_token");
    if (err == ESP_ERR_NVS_NOT_FOUND && !token)
        err = ESP_OK;
    if (err == ESP_OK) {
        esp_err_t erased = nvs_erase_key(handle, "access_token");
        if (erased != ESP_OK && erased != ESP_ERR_NVS_NOT_FOUND)
            err = erased;
    }
    if (err == ESP_OK)
        err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t token_storage_read(char **token)
{
    if (!token)
        return ESP_ERR_INVALID_ARG;
    *token = NULL;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("graphapi", NVS_READONLY, &handle);
    if (err != ESP_OK)
        return err == ESP_ERR_NVS_NOT_FOUND ? ESP_ERR_NOT_FOUND : err;
    size_t size = 0;
    err = nvs_get_str(handle, "refresh_token", NULL, &size);
    if ((err == ESP_OK && (size < 2 || size > TOKEN_LIMIT + 1)) ||
        err == ESP_ERR_NVS_TYPE_MISMATCH) {
        nvs_close(handle);
        err = token_storage_write(NULL);
        return err == ESP_OK ? ESP_ERR_NOT_FOUND : err;
    }
    if (err == ESP_OK) {
        *token = calloc(size, 1);
        if (!*token)
            err = ESP_ERR_NO_MEM;
        else {
            (*token)[0] = '\0';
            err = nvs_get_str(handle, "refresh_token", *token, &size);
        }
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        secret_free(*token);
        *token = NULL;
    }
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_ERR_NOT_FOUND : err;
}
