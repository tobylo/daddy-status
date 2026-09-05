#ifndef DADDY_AUTH_CLIENT_H
#define DADDY_AUTH_CLIENT_H
#include "esp_err.h"
#include "service_error.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char *access_token;
    int64_t access_deadline;
    service_error_t error;
} auth_client_t;

typedef enum { AUTH_WAITING, AUTH_CODE_READY, AUTH_SIGNED_IN, AUTH_RETRYING } auth_event_t;
/* Register before starting the worker. Callback must copy borrowed code synchronously.
 * Only user_code is published; opaque device/access/refresh tokens stay private. */
typedef void (*auth_observer_t)(auth_event_t event, const char *user_code, int64_t deadline);
void auth_client_observe(auth_observer_t observer);

/* Zero-initialize; a single worker owns each instance and its token. */
bool auth_client_ready(const auth_client_t *auth);
esp_err_t auth_client_ensure(auth_client_t *auth, unsigned *retry_after);
void auth_client_invalidate(auth_client_t *auth);
#endif
