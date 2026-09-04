#ifndef DADDY_GRAPH_CLIENT_H
#define DADDY_GRAPH_CLIENT_H
#include "app_state.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
/* queue must have length one and hold app_status_t snapshots. */
esp_err_t graph_client_init(QueueHandle_t queue);
#endif
