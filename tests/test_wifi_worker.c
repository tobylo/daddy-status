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
static void got_ip(void)
{
    ip_event_got_ip_t ip = {0};
    event_handler(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, &ip);
    in_flight = false;
}

static void expect_association(unsigned expected_attempts)
{
    assert(attempts == expected_attempts);
    assert(in_flight);
    wifi_event_sta_connected_t associated = {.channel = 6};
    event_handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &associated);
    assert(!wifi_is_connected() && in_flight);
    got_ip();
}

static void first_association(void)
{
    expect_association(2);
}

static void final_association(void)
{
    expect_association(6);
}

static void finish_worker(void)
{
    longjmp(done, 1);
}

static void recovery_connected(void)
{
    assert(recovery);
    assert(recovery_entries == 1);
    assert(mode_failures == 1);
    got_ip();
}

static void expect_recovery_disabled(void)
{
    assert(!recovery);
    assert(recovery_exits == 1);
}

static void retry_disconnected(void)
{
    assert(attempts == 2);
    disconnected();
}

static void retries_finished(void)
{
    assert(attempts == 6);
    finish_worker();
}

typedef struct {
    EventBits_t mask;
    unsigned repeats;
    int64_t until_us; /* Repeat until this simulated time, including worker delays. */
    void (*action)(void);
    void (*check)(void); /* Validate each wait, including repeated timeouts. */
} wait_step_t;

#define CONNECTION_RESULT (CONNECTED_BIT | DISCONNECTED_BIT)
static const wait_step_t retry_waits[] = {
    {CONNECTION_RESULT, 1, 0, first_association, NULL},
    {DISCONNECTED_BIT, 1, 0, retry_disconnected, NULL},
    {CONNECTION_RESULT, 30, 0, NULL, NULL}, /* No association/DHCP response. */
    {CONNECTION_RESULT, 30, 0, NULL, NULL},
    {DISCONNECTED_BIT, 5, 0, NULL, NULL}, /* Missing cancellation acknowledgement. */
    {CONNECTION_RESULT, 1, 0, disconnected, NULL},
    {CONNECTION_RESULT, 1, 0, final_association, NULL},
    {DISCONNECTED_BIT, 1, 0, retries_finished, NULL},
};
static const wait_step_t recovery_waits[] = {
    {CONNECTION_RESULT, 0, 127000000, NULL, NULL},
    {DISCONNECTED_BIT, 5, 0, NULL, NULL}, /* Fourth cancellation goes unacknowledged. */
    {CONNECTION_RESULT, 0, 182000000, recovery_connected, NULL},
    {DISCONNECTED_BIT, 0, 400000000, disconnected, expect_recovery_disabled},
};
static const wait_step_t *wait_script;
static size_t wait_count, wait_pos;
static unsigned step_waits;

static EventBits_t scripted_wait(EventBits_t mask, TickType_t ticks)
{
    assert(ticks == task_ticks_ms(1000));
    if (bits & mask)
        return bits;
    assert(wait_pos < wait_count);
    const wait_step_t *step = &wait_script[wait_pos];
    assert(mask == step->mask);
    if (mask == CONNECTION_RESULT)
        assert(in_flight);
    now += 1000000;
    if (step->check)
        step->check();
    ++step_waits;
    if (step_waits < step->repeats || now < step->until_us)
        return bits;
    ++wait_pos;
    step_waits = 0;
    if (step->action)
        step->action();
    return bits;
}

static EventBits_t initialize_worker(void)
{
    assert(!worker_context && run_worker);
    worker_context = true;
    if (!setjmp(done))
        run_worker(NULL);
    worker_context = false;
    assert(bits & INITIALIZED_BIT);
    assert(wait_pos == wait_count);
    return bits;
}

EventBits_t xEventGroupWaitBits(EventGroupHandle_t g, EventBits_t mask, BaseType_t clear,
                                BaseType_t all, TickType_t ticks)
{
    if (mask == INITIALIZED_BIT)
        return initialize_worker();
    if (mask == CONNECTED_BIT) {
        ++waits;
        return bits;
    }
    assert(worker_context);
    if (mask != STARTED_BIT)
        return scripted_wait(mask, ticks);
    assert(bits & STARTED_BIT);
    EventBits_t result = bits;
    if (clear)
        bits &= ~mask;
    return result;
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
static esp_err_t recovery_mode_result(int mode)
{
    if (scenario != 1 || mode != WIFI_MODE_APSTA)
        return ESP_OK;
    assert(now >= 180000000 && now <= 181000000);
    if (mode_failures)
        return ESP_OK;
    ++mode_failures;
    return ESP_FAIL;
}

esp_err_t esp_wifi_set_mode(int mode)
{
    assert(worker_context);
    if (bits & INITIALIZED_BIT) {
        if (recovery_mode_result(mode) != ESP_OK)
            return ESP_FAIL;
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
typedef struct {
    unsigned backoff_seconds;
    esp_err_t result;
    bool acknowledge_cancel;
    int64_t cancel_after_us;
} connect_step_t;
static const connect_step_t retry_connects[] = {
    {0, ESP_FAIL, true, 0},       /* Immediate connection failure. */
    {1, ESP_OK, true, 0},         /* Connect, then disconnect. */
    {1, ESP_OK, true, 30000000},  /* DHCP timeout with cancellation acknowledgement. */
    {2, ESP_OK, false, 30000000}, /* DHCP timeout requires a driver restart. */
    {4, ESP_OK, true, 0},         /* Driver reports disconnection during connect. */
    {8, ESP_OK, true, 0},         /* Successful retry. */
};

static esp_err_t retry_connect(void)
{
    assert(attempts < sizeof(retry_connects) / sizeof(retry_connects[0]));
    const connect_step_t *step = &retry_connects[attempts];
    assert(now - backoff_started == (int64_t)step->backoff_seconds * 1000000);
    return step->result;
}

static void check_recovery_retry(void)
{
    if (now < 400000000)
        return;
    assert(!recovery && last_online >= 399000000);
    finish_worker();
}

esp_err_t esp_wifi_connect(void)
{
    assert(!in_flight && !wifi_is_connected());
    assert(worker_context);
    esp_err_t result = ESP_OK;
    if (scenario == 0)
        result = retry_connect();
    else
        check_recovery_retry();
    attempt_started = now;
    ++attempts;
    in_flight = result == ESP_OK;
    return result;
}

static bool cancellation_acknowledged(void)
{
    if (scenario != 0)
        return attempts != 4;
    assert(attempts > 0 && attempts <= sizeof(retry_connects) / sizeof(retry_connects[0]));
    const connect_step_t *step = &retry_connects[attempts - 1];
    assert(now - attempt_started == step->cancel_after_us);
    return step->acknowledge_cancel;
}

esp_err_t esp_wifi_disconnect(void)
{
    assert(worker_context);
    ++cancellations;
    if (cancellation_acknowledged())
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
    wait_pos = step_waits = 0;
    wait_script = NULL;
    wait_count = 0;
}

static void test_retries(void)
{
    reset(0);
    wait_script = retry_waits;
    wait_count = sizeof(retry_waits) / sizeof(retry_waits[0]);
    assert(!wifi_is_connected());
    wifi_init();
    assert(attempts == 6 && delays == 16 && cancellations == 3 && stops == 1 &&
           wifi_is_connected());
    wifi_wait_connected();
    assert(waits == 1);
}

static void test_recovery(void)
{
    reset(1);
    wait_script = recovery_waits;
    wait_count = sizeof(recovery_waits) / sizeof(recovery_waits[0]);
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
}

static void test_missing_ssid(void)
{
    reset(2);
    settings.ssid[0] = 0;
    wifi_init();
    wifi_diagnostics_t snapshot;
    wifi_diagnostics_snapshot(&snapshot);
    assert(snapshot.recovery_ap);
}

int main(void)
{
    test_retries();
    test_recovery();
    test_missing_ssid();
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
