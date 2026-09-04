#ifndef DADDY_TOKEN_STORAGE_H
#define DADDY_TOKEN_STORAGE_H
#include "esp_err.h"
/* Caller owns *token; free with secret_free. Invalid stored tokens are erased.
 * ESP_ERR_NOT_FOUND means login is needed; other errors must be retried/reported. */
esp_err_t token_storage_read(char **token);
/* NULL erases authorization. Always removes obsolete persisted access tokens. */
esp_err_t token_storage_write(const char *token);
#endif
