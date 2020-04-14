#ifndef _GRAPH_CLIENT_H_
#define _GRAPH_CLIENT_H_

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"

/*
#ifndef SWITCH_CASE_INIT
#define SWITCH_CASE_INIT
    #define SWITCH(X)   for (char* __switch_p__ = X, int __switch_next__=1 ; __switch_p__ ; __switch_p__=0, __switch_next__=1) { {
    #define CASE(X)         } if (!__switch_next__ || !(__switch_next__ = strcmp(__switch_p__, X))) {
    #define DEFAULT         } {
    #define END         }}
#endif
*/

esp_err_t graph_client_init(QueueHandle_t *queue);

typedef struct {
    char value[23];
    unsigned long hash;
    unsigned int bit_flag;
} teams_presence_t;

static teams_presence_t presence_lookup[] = {
    { .value = "Available", .bit_flag = 0 },
    { .value = "AvailableIdle", .bit_flag = 0 },
    { .value = "Offline", .bit_flag = 0 },
    { .value = "OffWork", .bit_flag = 0 },
    { .value = "OutOfOffice", .bit_flag = 0 },
    { .value = "Away", .bit_flag = 0 },
    { .value = "BeRightBack", .bit_flag = 0 },
    { .value = "Inactive", .bit_flag = 0 },
    { .value = "PresenceUnknown", .bit_flag = 0 },

    { .value = "Busy", .bit_flag = 1 },
    { .value = "BusyIdle", .bit_flag = 1 },
    
    { .value = "DoNotDisturb", .bit_flag = 2 },
    { .value = "InACall", .bit_flag = 2 },

    { .value = "InAConferenceCall", .bit_flag = 4 },
    { .value = "InAMeeting", .bit_flag = 4 },
    { .value = "Presenting", .bit_flag = 4 },
    { .value = "UrgentInterruptionsOnly", .bit_flag = 4 }
};

#define PRESENCE_AVAILABLE 0U
#define PRESENCE_BUSY 1U
#define PRESENCE_DO_NOT_DISTURB 2U

#endif