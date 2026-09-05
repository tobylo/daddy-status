#include "FreeRTOS.h"
typedef void *EventGroupHandle_t;
typedef uint32_t EventBits_t;
#define BIT0 1U
#define BIT1 2U
#define BIT2 4U
EventGroupHandle_t xEventGroupCreate(void);
EventBits_t xEventGroupSetBits(EventGroupHandle_t, EventBits_t);
EventBits_t xEventGroupClearBits(EventGroupHandle_t, EventBits_t);
EventBits_t xEventGroupGetBits(EventGroupHandle_t);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t, EventBits_t, BaseType_t, BaseType_t,
                                TickType_t);
