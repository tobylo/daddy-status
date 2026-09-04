#ifndef DADDY_PRESENCE_H
#define DADDY_PRESENCE_H
#include "cJSON.h"
typedef enum {
    PRESENCE_AVAILABLE,
    PRESENCE_BUSY,
    PRESENCE_DO_NOT_DISTURB,
    PRESENCE_OFF_WORK,
    PRESENCE_UNKNOWN,
} presence_t;
presence_t presence_from_json(const cJSON *root);
#endif
