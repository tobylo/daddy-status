#ifndef DADDY_IMPROV_H
#define DADDY_IMPROV_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    void (*write)(const uint8_t *data, size_t length);
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

void improv_init(improv_t *service, improv_io_t io, bool trial);
void improv_feed(improv_t *service, uint8_t byte, int64_t now);
/* A failed persisted trial reports its error before the main task reboots. */
void improv_poll(improv_t *service, const char *url, bool trial, bool expired);
void improv_serial_init(void);
void improv_serial_tick(int64_t now, bool reboot_due);
#endif
