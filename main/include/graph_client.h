#ifndef _GRAPH_CLIENT_H_
#define _GRAPH_CLIENT_H_

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "presence.h"

esp_err_t graph_client_init(QueueHandle_t *queue);

#define STATE_TOKEN_REFRESH 4U
#define STATE_TOKEN_RECEIVED 5U
#define STATE_FAILED 6U

#endif