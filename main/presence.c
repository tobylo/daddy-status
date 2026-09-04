#include "presence.h"
#include "protocol.h"
#include <string.h>

presence_t presence_from_json(const cJSON *root)
{
    const char *activity = json_string(root, "activity");
    if (!activity) return PRESENCE_UNKNOWN;
    static const struct { const char *activity; presence_t state; } states[] = {
        {"Available", PRESENCE_AVAILABLE}, {"AvailableIdle", PRESENCE_AVAILABLE},
        {"Away", PRESENCE_AVAILABLE}, {"BeRightBack", PRESENCE_AVAILABLE},
        {"Inactive", PRESENCE_AVAILABLE}, {"Busy", PRESENCE_BUSY}, {"BusyIdle", PRESENCE_BUSY},
        {"DoNotDisturb", PRESENCE_DO_NOT_DISTURB}, {"InACall", PRESENCE_DO_NOT_DISTURB},
        {"InAConferenceCall", PRESENCE_DO_NOT_DISTURB}, {"InAMeeting", PRESENCE_DO_NOT_DISTURB},
        {"Presenting", PRESENCE_DO_NOT_DISTURB}, {"UrgentInterruptionsOnly", PRESENCE_DO_NOT_DISTURB},
        {"Offline", PRESENCE_OFF_WORK}, {"OffWork", PRESENCE_OFF_WORK}, {"OutOfOffice", PRESENCE_OFF_WORK},
    };
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
        if (!strcmp(activity, states[i].activity)) return states[i].state;
    }
    return PRESENCE_UNKNOWN;
}

