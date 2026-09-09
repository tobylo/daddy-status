#include "improv.h"
#include <string.h>

#define AUTHORIZED 2
#define PROVISIONING 3
#define PROVISIONED 4

static void packet(improv_t *s, uint8_t type, const uint8_t *data, size_t length)
{
    uint8_t frame[265] = {'I', 'M', 'P', 'R', 'O', 'V', 1, type, (uint8_t)length};
    memcpy(frame + 9, data, length);
    uint8_t checksum = 0;
    for (size_t i = 0; i < length + 9; ++i)
        checksum += frame[i];
    frame[length + 9] = checksum;
    s->io.write(frame, length + 10);
}

static void error(improv_t *s, uint8_t code)
{
    packet(s, 2, &code, 1);
}

static void state(improv_t *s, uint8_t value)
{
    s->state = value;
    packet(s, 1, &value, 1);
}

static void result(improv_t *s, uint8_t command, const char **strings, size_t count)
{
    uint8_t data[255] = {command, 0};
    size_t used = 2;
    for (size_t i = 0; i < count; ++i) {
        size_t length = strlen(strings[i]);
        if (used >= sizeof(data) || length > sizeof(data) - used - 1) {
            error(s, 255);
            return;
        }
        data[used++] = (uint8_t)length;
        memcpy(data + used, strings[i], length);
        used += length;
    }
    data[1] = (uint8_t)(used - 2);
    packet(s, 4, data, used);
}

static bool take_string(const uint8_t **data, size_t *left, char *out, size_t capacity)
{
    if (!*left)
        return false;
    size_t length = *(*data)++;
    --*left;
    if (length >= capacity || length > *left)
        return false;
    if (memchr(*data, 0, length))
        return false;
    memcpy(out, *data, length);
    out[length] = 0;
    *data += length;
    *left -= length;
    return true;
}

static void wifi_settings(improv_t *s, const uint8_t *data, size_t length)
{
    char ssid[33] = {0}, password[65] = {0};
    bool valid = take_string(&data, &length, ssid, sizeof(ssid)) &&
                 take_string(&data, &length, password, sizeof(password));
    uint8_t code = 1;
    if (length || !ssid[0])
        valid = false;
    if (valid)
        code = s->io.save_wifi(ssid, password);
    /* Do not retain credentials in the parser or stack after dispatch. */
    volatile char *secret = password;
    for (size_t i = 0; i < sizeof(password); ++i)
        secret[i] = 0;
    if (code) {
        error(s, code);
        return;
    }
    s->pending_result = 1;
    state(s, PROVISIONING);
}

static void empty_command(improv_t *s, uint8_t command)
{
    switch (command) {
    case 2:
        state(s, s->state);
        /* poll supplies the current IP, including after a serial reconnect. */
        if (s->state == PROVISIONED)
            s->pending_result = 2;
        break;
    case 3: {
        const char *info[] = {"Daddy Status", s->io.version, "ESP32", "Daddy Status"};
        result(s, 3, info, 4);
        break;
    }
    default:
        error(s, 2);
        break;
    }
}

static void dispatch(improv_t *s, const uint8_t *data, size_t length)
{
    error(s, 0);
    if (length < 2 || data[1] != length - 2) {
        error(s, 1);
        return;
    }
    if (data[0] == 1) {
        wifi_settings(s, data + 2, length - 2);
        return;
    }
    if (data[1]) {
        error(s, 1);
        return;
    }
    empty_command(s, data[0]);
}

static void clear_frame(improv_t *s)
{
    volatile uint8_t *data = s->frame;
    for (size_t i = 0; i < sizeof(s->frame); ++i)
        data[i] = 0;
    s->used = 0;
}

static void complete_frame(improv_t *s)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < s->used - 1; ++i)
        sum += s->frame[i];
    if (sum != s->frame[s->used - 1])
        error(s, 1);
    else
        dispatch(s, s->frame + 9, s->frame[8]);
    clear_frame(s);
}

void improv_init(improv_t *s, improv_io_t io, bool trial)
{
    *s = (improv_t){
        .io = io, .state = trial ? PROVISIONING : AUTHORIZED, .pending_result = trial ? 1 : 0};
}

void improv_feed(improv_t *s, uint8_t byte, int64_t now)
{
    static const uint8_t prefix[] = {'I', 'M', 'P', 'R', 'O', 'V', 1, 3};
    if (s->used && now - s->last_byte > 1000000)
        clear_frame(s);
    s->last_byte = now;
    if (s->used < sizeof(prefix) && byte != prefix[s->used]) {
        clear_frame(s);
        if (byte == 'I')
            s->frame[s->used++] = byte;
        return;
    }
    s->frame[s->used++] = byte;
    if (s->used >= 10 && s->used == (size_t)s->frame[8] + 10)
        complete_frame(s);
}

static void provisioned(improv_t *s, const char *url)
{
    if (s->state != PROVISIONED)
        state(s, PROVISIONED);
    if (s->pending_result) {
        const char *urls[] = {url};
        result(s, s->pending_result, urls, 1);
        s->pending_result = 0;
    }
}

void improv_poll(improv_t *s, const char *url, bool trial, bool expired)
{
    if (expired && s->state == PROVISIONING) {
        error(s, 3);
        state(s, AUTHORIZED);
        s->pending_result = 0;
        return;
    }
    if (!url || !*url) {
        if (s->state == PROVISIONED)
            state(s, AUTHORIZED);
        return;
    }
    /* A trial is successful only once the settings module commits it. */
    if (!trial)
        provisioned(s, url);
}
