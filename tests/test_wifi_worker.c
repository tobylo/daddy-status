#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#define ESP_ERROR_CHECK(err) assert((err) == ESP_OK)
#include "../main/wifi.c"
static jmp_buf done;
static EventBits_t bits;
static unsigned attempts, waits, delays, cancellations, stops;
static void (*run_worker)(void *);
static esp_netif_t netif;
static bool in_flight;
static void disconnected(void)
{
    in_flight = false;
    wifi_event_sta_disconnected_t event = {.reason = 2, .rssi = -45};
    event_handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event);
}
EventGroupHandle_t xEventGroupCreate(void)
{
    return (void *)1;
}
EventBits_t xEventGroupSetBits(EventGroupHandle_t g, EventBits_t b)
{
    return bits |= b;
}
EventBits_t xEventGroupClearBits(EventGroupHandle_t g, EventBits_t b)
{
    return bits &= ~b;
}
EventBits_t xEventGroupGetBits(EventGroupHandle_t g)
{
    return bits;
}
EventBits_t xEventGroupWaitBits(EventGroupHandle_t g, EventBits_t mask, BaseType_t clear,
                                BaseType_t all, TickType_t ticks)
{
    if (mask == CONNECTED_BIT) {
        ++waits;
        return bits;
    }
    if (mask == STARTED_BIT) {
        assert(bits & STARTED_BIT);
        EventBits_t result = bits;
        if (clear)
            bits &= ~mask;
        return result;
    }
    if (mask == (CONNECTED_BIT | DISCONNECTED_BIT)) {
        assert(ticks == pdMS_TO_TICKS(30000) && in_flight);
        if (attempts == 3 || attempts == 4)
            return 0; /* No association/DHCP response. */
        if (attempts == 5) {
            disconnected();
            return bits;
        }
        assert(attempts == 2 || attempts == 6);
        wifi_event_sta_connected_t associated = {.channel = 6};
        event_handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &associated);
        assert(!wifi_is_connected() && in_flight);
        ip_event_got_ip_t ip = {0};
        event_handler(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, &ip);
        in_flight = false;
        return bits;
    }
    assert(mask == DISCONNECTED_BIT);
    if (ticks == pdMS_TO_TICKS(5000))
        return bits; /* Cancellation acknowledgement, or missing. */
    assert(ticks == portMAX_DELAY && wifi_is_connected());
    if (attempts == 6)
        longjmp(done, 1);
    assert(attempts == 2);
    disconnected();
    return bits;
}

esp_err_t esp_netif_init(void)
{
    return ESP_OK;
}
esp_err_t esp_event_loop_create_default(void)
{
    return ESP_OK;
}
esp_netif_t *esp_netif_create_default_wifi_sta(void)
{
    return &netif;
}
esp_err_t esp_netif_set_hostname(esp_netif_t *n, const char *name)
{
    assert(!strcmp(name, "daddy-status"));
    return ESP_OK;
}
esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id,
                                     void (*fn)(void *, esp_event_base_t, int32_t, void *),
                                     void *arg)
{
    assert(fn == event_handler);
    return ESP_OK;
}
esp_err_t esp_wifi_init(const wifi_init_config_t *c)
{
    return ESP_OK;
}
esp_err_t esp_wifi_set_storage(int mode)
{
    assert(mode == WIFI_STORAGE_RAM);
    return ESP_OK;
}
esp_err_t esp_wifi_set_mode(int mode)
{
    return ESP_OK;
}
esp_err_t esp_wifi_set_config(int iface, const wifi_config_t *c)
{
    assert(!strcmp(c->sta.ssid, CONFIG_WIFI_SSID));
    return ESP_OK;
}
esp_err_t esp_wifi_start(void)
{
    event_handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_START, NULL);
    return ESP_OK;
}
esp_err_t esp_wifi_set_ps(int mode)
{
    return ESP_OK;
}
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg,
                       unsigned priority, TaskHandle_t *handle)
{
    run_worker = fn;
    *handle = (void *)2;
    return pdPASS;
}
esp_err_t esp_wifi_connect(void)
{
    assert(!in_flight && !wifi_is_connected());
    ++attempts;
    if (attempts == 1)
        return ESP_FAIL;
    in_flight = true;
    return ESP_OK;
}
esp_err_t esp_wifi_disconnect(void)
{
    ++cancellations;
    if (attempts != 4)
        disconnected();
    return ESP_OK;
}
esp_err_t esp_wifi_stop(void)
{
    ++stops;
    in_flight = false;
    disconnected();
    return ESP_OK;
}
void vTaskDelay(TickType_t ticks)
{
    const unsigned expected[] = {1, 1, 2, 4, 8};
    assert(delays < sizeof(expected) / sizeof(expected[0]));
    assert(ticks == pdMS_TO_TICKS(expected[delays++] * 1000));
}

int main(void)
{
    assert(!wifi_is_connected());
    wifi_init();
    if (!setjmp(done))
        run_worker(NULL);
    assert(attempts == 6 && delays == 5 && cancellations == 3 && stops == 1 && wifi_is_connected());
    wifi_wait_connected();
    assert(waits == 1);
    wifi_event_sta_disconnected_t disconnect = {.reason = 2, .rssi = -45};
    event_handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect);
    assert(!wifi_is_connected());
    puts("Wi-Fi startup, retry backoff, association/DHCP distinction, and disconnect tests passed");
}
