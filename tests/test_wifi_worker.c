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
static int selected_mode;
static bool ap_configured, worker_context;
static int64_t now, attempt_started, backoff_started;
static unsigned scenario, mode_failures, recovery_entries, recovery_exits;
static frame_settings_t settings = {.ssid = CONFIG_WIFI_SSID, .password = CONFIG_WIFI_PASSWORD};
const frame_settings_t *settings_get(void)
{
    return &settings;
}
static void disconnected(void)
{
    in_flight = false;
    backoff_started = now;
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
    if (mask == INITIALIZED_BIT) {
        assert(!worker_context && run_worker);
        worker_context = true;
        if (!setjmp(done))
            run_worker(NULL);
        worker_context = false;
        assert(bits & INITIALIZED_BIT);
        return bits;
    }
    if (mask == CONNECTED_BIT) {
        ++waits;
        return bits;
    }
    assert(worker_context);
    if (mask == STARTED_BIT) {
        assert(bits & STARTED_BIT);
        EventBits_t result = bits;
        if (clear)
            bits &= ~mask;
        return result;
    }
    assert(ticks == task_ticks_ms(1000));
    if (bits & mask)
        return bits;
    now += 1000000;
    if (scenario == 1) {
        if (mask == (CONNECTED_BIT | DISCONNECTED_BIT)) {
            if (now < 182000000)
                return bits;
            assert(recovery && recovery_entries == 1 && mode_failures == 1);
            ip_event_got_ip_t ip = {0};
            event_handler(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, &ip);
            in_flight = false;
            return bits;
        }
        if (wifi_is_connected()) {
            assert(!recovery && recovery_exits == 1);
            if (now < 400000000)
                return bits; /* Healthy connection keeps the recovery timer fresh. */
            disconnected();
            return bits;
        }
        return bits;
    }
    if (mask == (CONNECTED_BIT | DISCONNECTED_BIT)) {
        assert(in_flight);
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
    if (!wifi_is_connected())
        return bits; /* Missing cancellation acknowledgement. */
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
    assert(worker_context);
    return ESP_OK;
}
esp_err_t esp_wifi_set_storage(int mode)
{
    assert(worker_context);
    assert(mode == WIFI_STORAGE_RAM);
    return ESP_OK;
}
esp_err_t esp_wifi_set_mode(int mode)
{
    assert(worker_context);
    if (bits & INITIALIZED_BIT) {
        if (scenario == 1 && mode == WIFI_MODE_APSTA)
            assert(now >= 180000000 && now <= 181000000);
        if (scenario == 1 && mode == WIFI_MODE_APSTA && !mode_failures) {
            ++mode_failures;
            return ESP_FAIL;
        }
        if (mode == WIFI_MODE_APSTA)
            ++recovery_entries;
        else
            ++recovery_exits;
    }
    selected_mode = mode;
    return ESP_OK;
}
esp_err_t esp_wifi_set_config(int iface, const wifi_config_t *c)
{
    assert(worker_context);
    if (iface == WIFI_IF_STA)
        assert(!strcmp(c->sta.ssid, settings.ssid));
    else {
        assert(selected_mode == WIFI_MODE_APSTA);
        ap_configured = true;
        assert(c->ap.authmode == WIFI_AUTH_WPA2_PSK);
        assert(!strcmp(c->ap.password, CONFIG_SETUP_PASSWORD));
    }
    return ESP_OK;
}
esp_err_t esp_wifi_start(void)
{
    assert(worker_context);
    assert(ap_configured);
    event_handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_START, NULL);
    return ESP_OK;
}
esp_err_t esp_wifi_set_ps(int mode)
{
    assert(worker_context);
    return ESP_OK;
}
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *record)
{
    record->rssi = -40;
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
    assert(worker_context);
    if (scenario == 1 && now >= 400000000) {
        assert(!recovery && last_online >= 399000000);
        longjmp(done, 1);
    }
    if (scenario == 0 && attempts) {
        const unsigned backoff[] = {1, 1, 2, 4, 8};
        assert(now - backoff_started == (int64_t)backoff[attempts - 1] * 1000000);
    }
    attempt_started = now;
    ++attempts;
    if (scenario == 0 && attempts == 1)
        return ESP_FAIL;
    in_flight = true;
    return ESP_OK;
}
esp_err_t esp_wifi_disconnect(void)
{
    assert(worker_context);
    if (scenario == 0 && (attempts == 3 || attempts == 4))
        assert(now - attempt_started == 30000000);
    ++cancellations;
    if (attempts != 4)
        disconnected();
    return ESP_OK;
}
esp_err_t esp_wifi_stop(void)
{
    assert(worker_context);
    if (scenario == 0)
        assert(attempts == 4 && now - attempt_started == 35000000);
    ++stops;
    in_flight = false;
    disconnected();
    return ESP_OK;
}
void vTaskDelay(TickType_t ticks)
{
    assert(worker_context && ticks == task_ticks_ms(1000));
    now += 1000000;
    ++delays;
    if (scenario == 2 && now >= 1000000) {
        assert(recovery && selected_mode == WIFI_MODE_APSTA && !attempts);
        longjmp(done, 1);
    }
}

static void reset(unsigned next_scenario)
{
    bits = 0;
    attempts = waits = delays = cancellations = stops = 0;
    recovery_entries = recovery_exits = mode_failures = 0;
    now = attempt_started = backoff_started = 0;
    in_flight = ap_configured = recovery = false;
    wifi_events = NULL;
    memset(&diagnostics, 0, sizeof(diagnostics));
    scenario = next_scenario;
}

int main(void)
{
    assert(!wifi_is_connected());
    wifi_init();
    assert(attempts == 6 && delays == 16 && cancellations == 3 && stops == 1 &&
           wifi_is_connected());
    wifi_wait_connected();
    assert(waits == 1);
    reset(1);
    wifi_init();
    assert(recovery_entries == 1 && recovery_exits == 1);
    wifi_diagnostics_t snapshot;
    wifi_diagnostics_snapshot(&snapshot);
    assert(snapshot.has_signal && snapshot.rssi == -45);
    assert(snapshot.has_disconnect && snapshot.last_disconnect_reason == 2);
    assert(snapshot.event_count >= WIFI_EVENT_HISTORY_SIZE);
    assert(!snapshot.recovery_ap);
    bool enabled = false, disabled = false;
    for (unsigned i = 0; i < WIFI_EVENT_HISTORY_SIZE; ++i) {
        enabled |= snapshot.events[i].type == WIFI_DIAG_RECOVERY_AP_ENABLED;
        disabled |= snapshot.events[i].type == WIFI_DIAG_RECOVERY_AP_DISABLED;
    }
    assert(enabled && disabled);
    reset(2);
    settings.ssid[0] = 0;
    wifi_init();
    wifi_diagnostics_snapshot(&snapshot);
    assert(snapshot.recovery_ap);
    puts("Wi-Fi worker ownership, retries, DHCP timeout, and recovery tests passed");
}

int64_t esp_timer_get_time(void)
{
    return now;
}
esp_netif_t *esp_netif_create_default_wifi_ap(void)
{
    return &netif;
}
