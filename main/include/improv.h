#ifndef DADDY_IMPROV_H
#define DADDY_IMPROV_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Callbacks run synchronously on the caller task. version must outlive the service. */
typedef struct {
    void (*write)(const uint8_t *data, size_t length);
    /* Return an Improv error code; zero means the persisted trial was accepted. */
    uint8_t (*save_wifi)(const char *ssid, const char *password);
    const char *version;
} improv_io_t;

typedef struct {
    improv_io_t io;
    uint8_t frame[265];
    size_t used;
    uint8_t state;
    int64_t last_byte;
    uint8_t pending_result;
} improv_t;

/* Initialize parser state; trial resumes a pending WIFI_SETTINGS result after reboot. */
void improv_init(improv_t *service, improv_io_t io, bool trial);
/* Feed one raw UART byte; now is monotonic microseconds for partial-frame expiry. */
void improv_feed(improv_t *service, uint8_t byte, int64_t now);
/* Publish a URL only after trial commit; expired means the trial must roll back. */
void improv_poll(improv_t *service, const char *url, bool trial, bool expired);
/* Install the console RX driver after settings and Wi-Fi initialization. */
void improv_serial_init(void);
/* Call after settings_tick and before esp_restart to drain trial-failure output. */
void improv_serial_tick(int64_t now, bool reboot_due);
#endif
