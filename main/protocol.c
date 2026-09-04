#include "protocol.h"
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

bool response_append(response_buffer_t *buffer, const void *data, size_t length)
{
    if (!buffer || !buffer->data || !buffer->capacity || buffer->overflow)
        return false;
    if (buffer->length >= buffer->capacity || (!data && length) ||
        length >= buffer->capacity - buffer->length) {
        buffer->overflow = true;
        return false;
    }
    if (length)
        memcpy(buffer->data + buffer->length, data, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return true;
}

char *form_encode(const char *input)
{
    if (!input)
        return NULL;
    size_t length = strlen(input);
    if (length > (SIZE_MAX - 1) / 3)
        return NULL;
    char *output = malloc(length * 3 + 1);
    if (!output)
        return NULL;
    static const char hex[] = "0123456789ABCDEF";
    size_t j = 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)input[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '.' || c == '_' || c == '~') {
            output[j++] = (char)c;
        } else {
            output[j++] = '%';
            output[j++] = hex[c >> 4];
            output[j++] = hex[c & 15];
        }
    }
    output[j] = '\0';
    return output;
}

cJSON *response_json(const response_buffer_t *buffer)
{
    if (!buffer || !buffer->data || buffer->overflow || !buffer->length ||
        buffer->length >= buffer->capacity || buffer->data[buffer->length] != '\0' ||
        memchr(buffer->data, '\0', buffer->length))
        return NULL;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(buffer->data, buffer->length + 1, &end, true);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

const char *json_string(const cJSON *root, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsString(item) && item->valuestring && item->valuestring[0] ? item->valuestring
                                                                             : NULL;
}

bool json_seconds(const cJSON *root, const char *key, unsigned *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!value || !cJSON_IsNumber(item) || !isfinite(item->valuedouble) || item->valuedouble < 1 ||
        item->valuedouble > 86400 || floor(item->valuedouble) != item->valuedouble)
        return false;
    *value = (unsigned)item->valuedouble;
    return true;
}

unsigned retry_after_seconds(const char *value)
{
    if (!value || !*value)
        return 0;
    unsigned seconds = 0;
    for (const char *p = value; *p; ++p) {
        if (*p < '0' || *p > '9')
            return 0;
        unsigned digit = (unsigned)(*p - '0');
        seconds = seconds > (UINT_MAX - digit) / 10 ? UINT_MAX : seconds * 10 + digit;
    }
    return seconds > MAX_RETRY_AFTER_SECONDS ? MAX_RETRY_AFTER_SECONDS : seconds;
}

void secret_free(char *secret)
{
    if (!secret)
        return;
    size_t length = strlen(secret);
    volatile char *p = secret;
    while (length--)
        *p++ = 0;
    free(secret);
}

bool guid_valid(const char *value)
{
    if (!value || strlen(value) != 36)
        return false;
    for (size_t i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (value[i] != '-')
                return false;
        } else if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f') ||
                     (value[i] >= 'A' && value[i] <= 'F')))
            return false;
    }
    return true;
}

bool bearer_token_valid(const char *token)
{
    if (!token || !*token || strlen(token) > TOKEN_LIMIT)
        return false;
    for (const unsigned char *p = (const unsigned char *)token; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
              strchr("-._~+/=", *p)))
            return false;
    }
    return true;
}
