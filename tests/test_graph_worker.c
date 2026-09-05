#include "../main/graph_client.c"
#include "app_state.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static jmp_buf done;
static unsigned requests, logins, invalidations, wifi_waits, clock_waits;
static int64_t now = 1000000;
static unsigned delays[32], delay_count;
static app_status_t snapshots[32];
static unsigned snapshot_count;
static void (*run_worker)(void *);
static bool fail_task;
static const struct {
    int code;
    const char *body;
    unsigned retry;
    esp_err_t err;
} script[] = {
    {200, "{\"activity\":\"Available\"}", 0, ESP_OK},
    {401, "{}", 0, ESP_OK},
    {200, "{\"activity\":\"Busy\"}", 0, ESP_OK},
    {403, "{}", 90, ESP_OK},
    {429, "{}", 120, ESP_OK},
    {0, "", 0, ESP_FAIL},
    {200, "{\"activity\":\"Busy\"}", 0, ESP_OK},
};
int64_t esp_timer_get_time(void)
{
    return now;
}
uint32_t esp_random(void)
{
    return 0;
}
void wifi_wait_connected(void)
{
    ++wifi_waits;
    if (requests == sizeof(script) / sizeof(script[0]))
        longjmp(done, 1);
    /* A reconnect can consume arbitrary time before the next request. */
    if (requests == 6)
        now += 90000000;
}
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *c)
{
    assert(c->server);
    return ESP_OK;
}
esp_err_t esp_netif_sntp_sync_wait(TickType_t ticks)
{
    assert(ticks == pdMS_TO_TICKS(15000));
    return clock_waits++ == 0 ? ESP_ERR_TIMEOUT : ESP_OK;
}
bool auth_client_ready(const auth_client_t *auth)
{
    return auth->access_token != NULL;
}
esp_err_t auth_client_ensure(auth_client_t *auth, unsigned *retry)
{
    ++logins;
    *retry = 0;
    auth->access_token = "test-access";
    return ESP_OK;
}
void auth_client_invalidate(auth_client_t *auth)
{
    ++invalidations;
    auth->access_token = NULL;
}
esp_err_t http_request(const char *url, const char *form, const char *token, http_response_t *r)
{
    assert(!strcmp(url, "https://graph.microsoft.com/v1.0/me/presence") && !form && token);
    assert(requests < sizeof(script) / sizeof(script[0]));
    unsigned i = requests++;
    memset(r, 0, sizeof(*r));
    r->status = script[i].code;
    r->retry_after = script[i].retry;
    r->body.data = strdup(script[i].body);
    r->body.length = strlen(r->body.data);
    r->body.capacity = r->body.length + 1;
    now += 2000000;
    return script[i].err;
}
void http_response_free(http_response_t *r)
{
    free(r->body.data);
    memset(r, 0, sizeof(*r));
}
BaseType_t xQueueOverwrite(QueueHandle_t q, const void *item)
{
    assert(q == (void *)1 && snapshot_count < 32);
    snapshots[snapshot_count++] = *(const app_status_t *)item;
    return pdPASS;
}
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg,
                       unsigned priority, TaskHandle_t *handle)
{
    if (fail_task)
        return pdFAIL;
    run_worker = fn;
    *handle = (void *)2;
    return pdPASS;
}
void vTaskSuspend(TaskHandle_t task)
{
    assert(!"unexpected suspend");
}
void vTaskDelay(TickType_t ticks)
{
    assert(delay_count < 32);
    delays[delay_count++] = ticks;
    now += (int64_t)ticks * 1000000 / TEST_FREERTOS_HZ;
}
int main(void)
{
    assert(graph_client_init(NULL) == ESP_ERR_INVALID_ARG);
    fail_task = true;
    assert(graph_client_init((void *)1) == ESP_ERR_NO_MEM);
    fail_task = false;
    assert(graph_client_init((void *)1) == ESP_OK);
    assert(graph_client_init((void *)1) == ESP_ERR_INVALID_ARG);
    if (!setjmp(done))
        run_worker(NULL);
    assert(requests == 7 && logins == 2 && invalidations == 1 && clock_waits == 2);
    assert(wifi_waits >= 8);
    unsigned ready = 0, errors = 0;
    int64_t last_update = 0;
    for (unsigned i = 0; i < snapshot_count; i++) {
        app_status_t *s = &snapshots[i];
        if (s->service == SERVICE_READY) {
            ++ready;
            assert(s->updated_at_us > last_update);
            last_update = s->updated_at_us;
        } else if (s->service == SERVICE_ERROR && s->has_presence) {
            ++errors;
            assert(s->updated_at_us == last_update);
        }
    }
    assert(ready == 3 && errors == 4);
    unsigned permission = 0, throttle = 0, network = 0;
    for (unsigned i = 0; i < snapshot_count; i++) {
        permission += snapshots[i].error == SERVICE_ERROR_PERMISSION;
        throttle += snapshots[i].error == SERVICE_ERROR_THROTTLED;
        network += snapshots[i].error == SERVICE_ERROR_NETWORK;
    }
    assert(permission == 1 && throttle == 1 && network == 1);
    const unsigned expected[] = {8, 2, 0, 8, 60, 30, 0, 60, 60, 0, 8, 0, 8};
    assert(delay_count == sizeof(expected) / sizeof(expected[0]));
    for (unsigned i = 0; i < delay_count; ++i)
        assert(delays[i] == pdMS_TO_TICKS(expected[i] * 1000));
    app_status_t last = snapshots[snapshot_count - 1];
    assert(last.service == SERVICE_READY && last.presence == PRESENCE_BUSY);
    assert(app_display_mode(&last, true, last.updated_at_us + 60000000, 60000000) ==
           DISPLAY_UNKNOWN);
    puts("Graph worker startup, clock wait, 401, 403, throttling, outage, and recovery tests "
         "passed");
}
