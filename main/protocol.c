#include "protocol.h"
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool ascii_in_range(unsigned char c, unsigned char first, unsigned char last)
{
    return c >= first && c <= last;
}

static bool is_alnum_ascii(unsigned char c)
{
    return ascii_in_range(c, 'a', 'z') || ascii_in_range(c, 'A', 'Z') ||
           ascii_in_range(c, '0', '9');
}

static bool is_unreserved(unsigned char c)
{
    return is_alnum_ascii(c) || (c != '\0' && strchr("-._~", c));
}

static bool is_hex_ascii(unsigned char c)
{
    return c != '\0' && strchr("0123456789abcdefABCDEF", c);
}

static bool is_dash_index(size_t index)
{
    return index == 8 || index == 13 || index == 18 || index == 23;
}

static bool buffer_usable(const response_buffer_t *buffer)
{
    if (!buffer || !buffer->data)
        return false;
    return buffer->capacity != 0 && !buffer->overflow;
}

static bool buffer_can_append(const response_buffer_t *buffer, const void *data, size_t length)
{
    if (buffer->length >= buffer->capacity)
        return false;
    if (!data && length)
        return false;
    return length < buffer->capacity - buffer->length;
}

bool response_append(response_buffer_t *buffer, const void *data, size_t length)
{
    if (!buffer_usable(buffer))
        return false;
    if (!buffer_can_append(buffer, data, length)) {
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
        if (is_unreserved(c)) {
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

static bool buffer_is_c_string(const response_buffer_t *buffer)
{
    if (!buffer_usable(buffer))
        return false;
    if (!buffer->length || buffer->length >= buffer->capacity)
        return false;
    return buffer->data[buffer->length] == '\0' && !memchr(buffer->data, '\0', buffer->length);
}

cJSON *response_json(const response_buffer_t *buffer)
{
    if (!buffer_is_c_string(buffer))
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

static bool valid_seconds(double seconds)
{
    return isfinite(seconds) && seconds >= 1 && seconds <= 86400 && floor(seconds) == seconds;
}

bool json_seconds(const cJSON *root, const char *key, unsigned *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!value || !cJSON_IsNumber(item))
        return false;
    if (!valid_seconds(item->valuedouble))
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
        if (is_dash_index(i)) {
            if (value[i] != '-')
                return false;
        } else if (!is_hex_ascii((unsigned char)value[i]))
            return false;
    }
    return true;
}

bool bearer_token_valid(const char *token)
{
    if (!token || !*token)
        return false;
    if (strlen(token) > TOKEN_LIMIT)
        return false;
    for (const unsigned char *p = (const unsigned char *)token; *p; ++p) {
        if (!(is_alnum_ascii(*p) || strchr("-._~+/=", *p)))
            return false;
    }
    return true;
}
