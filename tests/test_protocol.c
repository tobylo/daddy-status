#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void buffers(void)
{
    char data[8] = {0};
    response_buffer_t buffer = {data, sizeof(data), 0, false};
    assert(response_append(&buffer, "abc", 3));
    assert(response_append(&buffer, "defg", 4));
    assert(!strcmp(data, "abcdefg"));
    assert(!response_append(&buffer, "!", 1));
    assert(buffer.overflow && !response_append(&buffer, "", 0));
    assert(!response_json(&buffer));
    assert(!response_append(NULL, "a", 1));
}

static void encoding(void)
{
    const char *inputs[] = {"", "abc-._~", " +&=/%", "\xff"};
    const char *outputs[] = {"", "abc-._~", "%20%2B%26%3D%2F%25", "%FF"};
    for (unsigned i = 0; i < 4; ++i) {
        char *encoded = form_encode(inputs[i]);
        assert(encoded && !strcmp(encoded, outputs[i]));
        secret_free(encoded);
    }
    assert(!form_encode(NULL));
}

static void json(void)
{
    char data[128] = {0};
    response_buffer_t buffer = {data, sizeof(data), 0, false};
    assert(response_append(&buffer, "{\"activity\":", 12));
    const char *tail = "\"Busy\",\"expires_in\":3600}";
    assert(response_append(&buffer, tail, strlen(tail)));
    cJSON *root = response_json(&buffer);
    assert(root && !strcmp(json_string(root, "activity"), "Busy"));
    unsigned seconds = 0;
    assert(json_seconds(root, "expires_in", &seconds) && seconds == 3600);
    assert(!json_string(root, "expires_in") && !json_string(root, "missing"));
    cJSON_Delete(root);
    const char *invalid[] = {"{", "[]", "null", "{}garbage", "{\"expires_in\":-1}", "{\"expires_in\":1.5}", "{\"expires_in\":1e999}"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        buffer.length = 0;
        assert(response_append(&buffer, invalid[i], strlen(invalid[i])));
        root = response_json(&buffer);
        assert(!json_seconds(root, "expires_in", &seconds));
        if (i < 4) assert(!root);
        cJSON_Delete(root);
    }
    buffer.length = 0;
    assert(response_append(&buffer, "{}\0extra", 8));
    assert(!response_json(&buffer));
}

int main(void)
{
    buffers(); encoding(); json();
    assert(retry_after_seconds("30") == 30);
    assert(retry_after_seconds("99999999999999999") == 3600);
    assert(retry_after_seconds("garbage") == 0);
    assert(retry_after_seconds("999999999x") == 0);
    assert(retry_after_seconds("-1") == 0);
    puts("protocol tests passed");
}
