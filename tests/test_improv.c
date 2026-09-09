#include "improv.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t output[4096];
static size_t written;
static unsigned saves;
static uint8_t save_error;
static int64_t now;
static improv_t service;

static void write_packet(const uint8_t *data, size_t size)
{
    assert(size >= 10 && size == (size_t)data[8] + 10);
    assert(!memcmp(data, "IMPROV\1", 7));
    uint8_t checksum = 0;
    for (size_t i = 0; i < size - 1; ++i)
        checksum += data[i];
    assert(checksum == data[size - 1]);
    assert(written + size <= sizeof(output));
    memcpy(output + written, data, size);
    written += size;
}

static uint8_t save_wifi(const char *ssid, const char *password)
{
    assert(!strcmp(ssid, "home"));
    assert(!strcmp(password, "password") || !*password);
    ++saves;
    return save_error;
}

static void reset(bool trial)
{
    written = saves = save_error = 0;
    improv_init(&service, (improv_io_t){write_packet, save_wifi, "v1.2.3"}, trial);
}

static void command(const uint8_t *data, size_t size, bool corrupt)
{
    uint8_t frame[265] = {'I', 'M', 'P', 'R', 'O', 'V', 1, 3, (uint8_t)size};
    memcpy(frame + 9, data, size);
    uint8_t checksum = 0;
    for (size_t i = 0; i < size + 9; ++i)
        checksum += frame[i];
    frame[size + 9] = checksum + corrupt;
    for (size_t i = 0; i < size + 10; ++i)
        improv_feed(&service, frame[i], now++);
    assert(service.used == 0);
    for (size_t i = 0; i < sizeof(service.frame); ++i)
        assert(service.frame[i] == 0);
}

static void last_scalar(uint8_t type, uint8_t value)
{
    assert(written >= 11);
    assert(output[written - 4] == type);
    assert(output[written - 3] == 1 && output[written - 2] == value);
}

static const uint8_t credentials[] = {1,   14,  4,   'h', 'o', 'm', 'e', 8,
                                      'p', 'a', 's', 's', 'w', 'o', 'r', 'd'};

static void test_provisioning(void)
{
    reset(false);
    command((uint8_t[]){2, 0}, 2, false);
    last_scalar(1, 2);
    command(credentials, sizeof(credentials), false);
    assert(saves == 1 && service.state == 3);
    command((uint8_t[]){2, 0}, 2, false);
    assert(service.pending_result == 1);
    improv_poll(&service, "http://192.168.1.2/", true, false);
    assert(service.state == 3);
    /* A reboot resumes the persisted trial without another WIFI_SETTINGS RPC. */
    reset(true);
    improv_poll(&service, "", true, false);
    assert(service.state == 3);
    improv_poll(&service, "http://192.168.1.2/", false, false);
    assert(service.state == 4 && service.pending_result == 0);
    assert(output[11 + 9] == 1);
    assert(!memcmp(output + 11 + 12, "http://192.168.1.2/", 19));
    written = 0;
    command((uint8_t[]){2, 0}, 2, false);
    improv_poll(&service, "http://192.168.1.2/", false, false);
    assert(output[22 + 9] == 2);
    improv_poll(&service, "", false, false);
    last_scalar(1, 2);
}

static void test_failures(void)
{
    reset(false);
    save_error = 255;
    command(credentials, sizeof(credentials), false);
    last_scalar(2, 255);
    assert(service.state == 2);
    reset(true);
    improv_poll(&service, "", true, true);
    assert(output[9] == 3);
    last_scalar(1, 2);
    assert(!service.pending_result);
}

static void test_malformed(void)
{
    reset(false);
    command(credentials, sizeof(credentials), true);
    last_scalar(2, 1);
    command((uint8_t[]){1, 0}, 2, false);
    last_scalar(2, 1);
    command((uint8_t[]){1, 3, 1, 0, 0}, 5, false);
    last_scalar(2, 1);
    command((uint8_t[]){1, 2, 32, 0}, 4, false);
    last_scalar(2, 1);
    command((uint8_t[]){2, 1, 0}, 3, false);
    last_scalar(2, 1);
    command((uint8_t[]){3, 1}, 2, false);
    last_scalar(2, 1);
    command((uint8_t[]){4, 0}, 2, false);
    last_scalar(2, 2);
    command((uint8_t[]){0}, 0, false);
    last_scalar(2, 1);
    assert(saves == 0);
    uint8_t maximum[255] = {99, 253};
    command(maximum, sizeof(maximum), false);
    last_scalar(2, 1);
}

static void test_stream(void)
{
    reset(false);
    const char *noise = "log line\r\nIIMPROV\2";
    for (const char *p = noise; *p; ++p)
        improv_feed(&service, *p, now++);
    command((uint8_t[]){3, 0}, 2, false);
    assert(output[11 + 7] == 4 && output[11 + 9] == 3);
    assert(output[11 + 12] == 'D');
    improv_feed(&service, 'I', now++);
    improv_feed(&service, 'M', now++);
    now += 1000001;
    command((uint8_t[]){2, 0}, 2, false);
    last_scalar(1, 2);
    command((uint8_t[]){1, 6, 4, 'h', 'o', 'm', 'e', 0}, 8, false);
    assert(saves == 1 && service.state == 3);
}

int main(void)
{
    test_provisioning();
    test_failures();
    test_malformed();
    test_stream();
    puts("Improv framing, checksums, stream recovery and provisioning states passed");
}
