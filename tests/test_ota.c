#include "../main/firmware_update.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static esp_partition_t running_slot = {.size = 8192}, other_slot = {.size = 8192};
static esp_ota_img_states_t boot_state;
static bool reserved, trial, interrupted, fail_write, bad_signature, fail_select, fail_confirm,
    slow, fail_begin;
static int begins, writes, ends, aborts, selections, confirms, rollbacks, restarts, status;
static size_t remaining;
static int64_t now;
const esp_partition_t *esp_ota_get_running_partition(void)
{
    return &running_slot;
}
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *p)
{
    return &other_slot;
}
esp_err_t esp_ota_get_state_partition(const esp_partition_t *p, esp_ota_img_states_t *s)
{
    *s = boot_state;
    return ESP_OK;
}
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void)
{
    ++confirms;
    return fail_confirm ? ESP_FAIL : ESP_OK;
}
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void)
{
    ++rollbacks;
    return ESP_FAIL;
}
void esp_restart(void)
{
    ++restarts;
}
int64_t esp_timer_get_time(void)
{
    return now;
}
bool settings_trial(void)
{
    return trial;
}
bool settings_update_begin(void)
{
    if (reserved || trial)
        return false;
    reserved = true;
    return true;
}
void settings_update_end(void)
{
    reserved = false;
}
esp_err_t esp_ota_begin(const esp_partition_t *p, size_t n, esp_ota_handle_t *h)
{
    assert(reserved && p == &other_slot);
    ++begins;
    *h = 1;
    return fail_begin ? ESP_FAIL : ESP_OK;
}
esp_err_t esp_ota_write(esp_ota_handle_t h, const void *p, size_t n)
{
    assert(h == 1);
    ++writes;
    return fail_write ? ESP_FAIL : ESP_OK;
}
esp_err_t esp_ota_end(esp_ota_handle_t h)
{
    ++ends;
    return bad_signature ? ESP_FAIL : ESP_OK;
}
esp_err_t esp_ota_abort(esp_ota_handle_t h)
{
    ++aborts;
    return ESP_OK;
}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p)
{
    assert(ends && !bad_signature);
    ++selections;
    return fail_select ? ESP_FAIL : ESP_OK;
}
int httpd_req_recv(httpd_req_t *req, char *buffer, size_t n)
{
    if (slow)
        now += 61000000;
    if (interrupted)
        return -1;
    if (n > 333)
        n = 333;
    assert(n <= remaining);
    remaining -= n;
    memset(buffer, 0xa5, n);
    return n;
}
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type)
{
    return ESP_OK;
}
esp_err_t httpd_resp_send(httpd_req_t *r, const char *b, int n)
{
    status = 200;
    return ESP_OK;
}
esp_err_t httpd_resp_send_err(httpd_req_t *r, int s, const char *b)
{
    status = s;
    return ESP_FAIL;
}
static void reset(void)
{
    reserved = trial = interrupted = fail_write = bad_signature = fail_select = fail_confirm =
        false;
    begins = writes = ends = aborts = selections = confirms = rollbacks = restarts = status = 0;
    slow = fail_begin = false;
    now = 1;
    connected_at = 0;
    boot_state = ESP_OTA_IMG_VALID;
    atomic_store(&restart_requested, false);
    firmware_update_init();
}
int main(void)
{
    httpd_req_t req = {.content_len = 5000};
    reset();
#if CONFIG_FRAME_OTA_ENABLE
    remaining = req.content_len;
    assert(firmware_update_upload(&req) == ESP_OK && status == 200);
    assert(writes > 1 && ends == 1 && selections == 1 && !aborts && reserved);
    firmware_update_tick(true, now);
    assert(restarts == 1);
    reset();
    bad_signature = true;
    remaining = req.content_len;
    assert(firmware_update_upload(&req) != ESP_OK);
    assert(ends == 1 && !selections && !aborts && !reserved && !atomic_load(&restart_requested));
    reset();
    fail_begin = true;
    assert(firmware_update_upload(&req) != ESP_OK && !aborts && !reserved);
    reset();
    slow = true;
    remaining = req.content_len;
    assert(firmware_update_upload(&req) != ESP_OK && aborts == 1 && !ends && !reserved);
    reset();
    interrupted = true;
    assert(firmware_update_upload(&req) != ESP_OK && aborts == 1 && !ends && !reserved);
    reset();
    fail_write = true;
    remaining = req.content_len;
    assert(firmware_update_upload(&req) != ESP_OK && aborts == 1 && !selections);
    reset();
    fail_select = true;
    remaining = req.content_len;
    assert(firmware_update_upload(&req) != ESP_OK && !reserved && !atomic_load(&restart_requested));
    reset();
    trial = true;
    assert(firmware_update_upload(&req) != ESP_OK && !begins);
    reset();
    reserved = true;
    assert(firmware_update_upload(&req) != ESP_OK && !begins);
    reset();
    req.content_len = 8193;
    assert(firmware_update_upload(&req) != ESP_OK && !begins);
    req.content_len = 0;
    assert(firmware_update_upload(&req) != ESP_OK && !begins);
    reset();
    boot_state = ESP_OTA_IMG_PENDING_VERIFY;
    req.content_len = 5000;
    assert(firmware_update_upload(&req) != ESP_OK && !begins);
#else
    assert(firmware_update_upload(&req) != ESP_OK && status == 403 && !begins);
    boot_state = ESP_OTA_IMG_PENDING_VERIFY;
#endif
    firmware_update_init();
    firmware_update_tick(true, 1);
    firmware_update_tick(true, 30000000);
    assert(!confirms);
    firmware_update_tick(false, 30000001);
    firmware_update_tick(true, 40000000);
    firmware_update_tick(true, 70000000);
    assert(confirms == 1 && !pending);
    reset();
    boot_state = ESP_OTA_IMG_PENDING_VERIFY;
    firmware_update_init();
    firmware_update_tick(false, 180000001);
    assert(rollbacks == 1 && restarts == 1);
    reset();
    boot_state = ESP_OTA_IMG_PENDING_VERIFY;
    firmware_update_init();
    fail_confirm = true;
    firmware_update_tick(true, 1);
    firmware_update_tick(true, 180000001);
    assert(confirms == 1 && pending && rollbacks == 1);
    puts("OTA upload failure cleanup, signature gate, boot selection and rollback health passed");
}
