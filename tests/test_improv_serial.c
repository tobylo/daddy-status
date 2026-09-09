#include <assert.h>
#define ESP_ERROR_CHECK(err) assert((err) == ESP_OK)
#define CONFIG_ESP_CONSOLE_UART 1
#define CONFIG_ESP_CONSOLE_UART_NUM 0
#include "../main/improv_serial.c"
#include <string.h>

static bool online, trial;
static unsigned writes, reads, drained;
static esp_err_t save_result;
static esp_netif_t netif;
static esp_app_desc_t app = {.version = "test"};
static uint8_t input[265];
static size_t input_length, input_at;

const esp_app_desc_t *esp_app_get_description(void)
{
    return &app;
}

esp_err_t uart_driver_install(int port, int rx, int tx, int queue_size, void *queue, int flags)
{
    assert(port == 0 && rx >= 265 && tx == 0);
    return ESP_OK;
}

int uart_read_bytes(int port, void *data, uint32_t length, TickType_t wait)
{
    ++reads;
    assert(length == 1 && wait == 0);
    if (input_at == input_length)
        return 0;
    *(uint8_t *)data = input[input_at++];
    return 1;
}

int uart_write_bytes(int port, const void *data, size_t length)
{
    const uint8_t *bytes = data;
    assert(bytes[0] == '\n' && bytes[length - 1] == '\n');
    assert(!memcmp(bytes + 1, "IMPROV", 6));
    assert(length == (size_t)bytes[9] + 12);
    ++writes;
    return length;
}

esp_err_t uart_wait_tx_done(int port, TickType_t wait)
{
    ++drained;
    assert(wait > 0);
    return ESP_OK;
}

bool wifi_is_connected(void)
{
    return online;
}

bool settings_trial(void)
{
    return trial;
}

esp_err_t settings_save_wifi(const char *ssid, const char *password)
{
    assert(!strcmp(ssid, "home") && !strcmp(password, "password"));
    return save_result;
}

esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key)
{
    assert(!strcmp(key, "WIFI_STA_DEF"));
    return &netif;
}

esp_err_t esp_netif_get_ip_info(esp_netif_t *n, esp_netif_ip_info_t *ip)
{
    ip->ip.addr = 1234;
    return ESP_OK;
}

static void boot(bool trying)
{
    restarting = false; /* BSS is cleared on a real restart. */
    trial = trying;
    writes = reads = drained = 0;
    input_at = input_length = 0;
    improv_serial_init();
}

static void test_trial_lifecycle(void)
{
    boot(true);
    online = true;
    improv_serial_tick(100, false);
    assert(service.state == 3 && writes == 0);
    trial = false; /* settings_tick committed the DHCP-successful trial. */
    improv_serial_tick(200, false);
    assert(service.state == 4 && writes == 2);
    boot(true);
    online = false;
    improv_serial_tick(180000000, true);
    assert(service.state == 2 && writes == 2 && drained == 1);
}

static void test_serial_dispatch(void)
{
    boot(false);
    uint8_t request[] = {'I', 'M', 'P', 'R', 'O', 'V', 1, 3, 2, 2, 0, 0};
    for (size_t i = 0; i < sizeof(request) - 1; ++i)
        request[sizeof(request) - 1] += request[i];
    memcpy(input, request, sizeof(request));
    input_length = sizeof(request);
    improv_serial_tick(1, false);
    assert(input_at == input_length && reads == input_length + 1 && writes == 2);
}

static void test_pending_restart(void)
{
    boot(false);
    save_result = ESP_ERR_INVALID_ARG;
    assert(save_wifi("home", "password") == 1 && !restarting);
    save_result = ESP_FAIL;
    assert(save_wifi("home", "password") == 255 && !restarting);
    save_result = ESP_OK;
    assert(save_wifi("home", "password") == 0 && restarting);
    online = true;
    service.state = 3;
    improv_serial_tick(100, false);
    assert(service.state == 3 && writes == 0);
}

int main(void)
{
    test_trial_lifecycle();
    test_serial_dispatch();
    test_pending_restart();
    puts("Improv UART dispatch, reboot handoff, trial commit and failure flush passed");
}
