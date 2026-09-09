#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_netif.h"
#include "improv.h"
#include "sdkconfig.h"
#include "settings.h"
#include "task_time.h"
#include "wifi.h"
#include <stdio.h>
#include <string.h>

#if !CONFIG_ESP_CONSOLE_UART
#error "Improv provisioning requires the console UART"
#endif

static improv_t service;
static bool restarting;

static void serial_write(const uint8_t *data, size_t length)
{
    /* ESP Web Tools recognizes packets at line boundaries. Fence off partial logs. */
    uint8_t framed[267];
    if (length > sizeof(framed) - 2)
        return;
    framed[0] = '\n';
    memcpy(framed + 1, data, length);
    framed[length + 1] = '\n';
    /* Share stdout's lock with ESP logging so logs cannot split a packet. */
    flockfile(stdout);
    fflush(stdout);
    uart_write_bytes(CONFIG_ESP_CONSOLE_UART_NUM, framed, length + 2);
    funlockfile(stdout);
}

static uint8_t save_wifi(const char *ssid, const char *password)
{
    esp_err_t err = settings_save_wifi(ssid, password);
    if (err == ESP_OK)
        restarting = true;
    return err == ESP_OK ? 0 : (err == ESP_ERR_INVALID_ARG ? 1 : 255);
}

void improv_serial_init(void)
{
    /* Board startup may already have installed the console driver. */
    if (!uart_is_driver_installed(CONFIG_ESP_CONSOLE_UART_NUM))
        ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 512, 0, 0, NULL, 0));
    improv_io_t io = {.write = serial_write,
                      .save_wifi = save_wifi,
                      .version = esp_app_get_description()->version};
    improv_init(&service, io, settings_trial());
}

static void station_url(char *url, size_t capacity)
{
    if (!wifi_is_connected())
        return;
    esp_netif_ip_info_t ip;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif || esp_netif_get_ip_info(netif, &ip) != ESP_OK)
        return;
    if (ip.ip.addr)
        snprintf(url, capacity, "http://" IPSTR "/", IP2STR(&ip.ip));
}

void improv_serial_tick(int64_t now, bool reboot_due)
{
    uint8_t byte;
    /* Bound input work so serial noise cannot starve the display/settings loop. */
    for (unsigned i = 0; i < 512; ++i) {
        if (uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, &byte, 1, 0) != 1)
            break;
        improv_feed(&service, byte, now);
    }
    /* The old station address must not complete a new trial. */
    if (!restarting) {
        char url[48] = {0};
        station_url(url, sizeof(url));
        bool trial = settings_trial();
        improv_poll(&service, url, trial, reboot_due && trial);
    }
    if (reboot_due)
        uart_wait_tx_done(CONFIG_ESP_CONSOLE_UART_NUM, task_ticks_ms(1000));
}
