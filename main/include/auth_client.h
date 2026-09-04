#ifndef DADDY_AUTH_CLIENT_H
#define DADDY_AUTH_CLIENT_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    char *access_token;
    int64_t access_deadline;
} auth_client_t;

/* Zero-initialize; a single worker owns each instance and its token. */
bool auth_client_ready(const auth_client_t *auth);
esp_err_t auth_client_ensure(auth_client_t *auth, unsigned *retry_after);
void auth_client_invalidate(auth_client_t *auth);
#endif
