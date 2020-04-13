#ifndef _GRAPH_CLIENT_H_
#define _GRAPH_CLIENT_H_

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"

esp_err_t graph_client_init(QueueHandle_t *queue);

#define PRESENCE_AVAILABLE 0U
#define PRESENCE_IN_CALL 1U
#define PRESENCE_IN_VIDEO_CALL 2U



#endif