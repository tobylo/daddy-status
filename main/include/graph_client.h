#ifndef _GRAPH_CLIENT_H_
#define _GRAPH_CLIENT_H_

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_system.h"

esp_err_t graph_client_init(QueueHandle_t *queue);

#define PRESENCE_AVAILABLE 0U
#define PRESENCE_BUSY 1U
#define PRESENCE_DO_NOT_DISTURB 2U
#define PRESENCE_OFF_WORK 3U
#define STATE_TOKEN_REFRESH 4U
#define STATE_TOKEN_RECEIVED 5U
#define STATE_FAILED 6U

#endif