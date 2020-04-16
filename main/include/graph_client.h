#ifndef _GRAPH_CLIENT_H_
#define _GRAPH_CLIENT_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

esp_err_t graph_client_init(QueueHandle_t *queue);
esp_err_t refresh_token();

#define PRESENCE_AVAILABLE 0U
#define PRESENCE_BUSY 1U
#define PRESENCE_DO_NOT_DISTURB 2U
#define PRESENCE_OFF_WORK 3U

#endif