#ifndef DADDY_PROTOCOL_H
#define DADDY_PROTOCOL_H

#include "cJSON.h"
#include <stdbool.h>
#include <stddef.h>

#define TOKEN_LIMIT 8192
/* Operational ceiling: retain multi-hour throttling without effectively disabling polling. */
#define MAX_RETRY_AFTER_SECONDS (24U * 60U * 60U)

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
bool guid_valid(const char *value);
bool bearer_token_valid(const char *token);

#endif
