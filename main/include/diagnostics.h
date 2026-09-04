#ifndef DADDY_DIAGNOSTICS_H
#define DADDY_DIAGNOSTICS_H
#include <stdint.h>
/* Each task owns its last_sample_us variable. Logs at most once per minute. */
void diagnostics_sample(const char *task, int64_t *last_sample_us);
#endif
