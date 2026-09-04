#ifndef DADDY_PROTOCOL_H
#define DADDY_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include "cJSON.h"

typedef struct {
    char *data;
    size_t capacity; /* Includes the terminating null byte. */
    size_t length;
    bool overflow;
} response_buffer_t;

bool response_append(response_buffer_t *buffer, const void *data, size_t length);
char *form_encode(const char *input);
cJSON *response_json(const response_buffer_t *buffer);
const char *json_string(const cJSON *root, const char *key);
bool json_seconds(const cJSON *root, const char *key, unsigned *value);
unsigned retry_after_seconds(const char *value);
void secret_free(char *secret);

#endif
