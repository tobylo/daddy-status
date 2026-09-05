#include "esp_err.h"
#include <stdint.h>
typedef int esp_event_base_t;
#define WIFI_EVENT 1
#define IP_EVENT 2
#define ESP_EVENT_ANY_ID -1
#define WIFI_EVENT_STA_START 1
#define WIFI_EVENT_STA_DISCONNECTED 2
#define WIFI_EVENT_STA_CONNECTED 3
#define IP_EVENT_STA_GOT_IP 4
esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_handler_register(esp_event_base_t, int32_t,
                                     void (*)(void *, esp_event_base_t, int32_t, void *), void *);
