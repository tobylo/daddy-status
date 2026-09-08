#include "firmware_update.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "settings.h"
#include <stdlib.h>

/* All reboot/health state belongs to the main task, except the atomic deadline. */
#include <stdatomic.h>
static atomic_bool restart_requested;
static bool pending;
static int64_t started, connected_at;

void firmware_update_init(void)
{
    esp_ota_img_states_t state;
    pending = esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
              state == ESP_OTA_IMG_PENDING_VERIFY;
    started = esp_timer_get_time();
}

void firmware_update_tick(bool connected, int64_t now)
{
    if (atomic_load(&restart_requested)) {
        esp_restart();
        return;
    }
    if (!pending)
        return;
    if (!connected)
        connected_at = 0;
    else if (!connected_at)
        connected_at = now;
    /* Exercise initialized services and the display loop, with 30s stable Wi-Fi.
     * Microsoft login/availability is deliberately not a boot health dependency. */
    if (connected_at && now - connected_at >= 30000000 && !settings_trial()) {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
            pending = false;
            ESP_LOGI("ota", "Firmware health check passed");
        }
    }
    if (pending && now - started >= 180000000) {
        ESP_LOGE("ota", "Firmware health check failed; rolling back");
        esp_ota_mark_app_invalid_rollback_and_reboot();
        esp_restart(); /* A failed rollback must not silently confirm this image. */
    }
}

esp_err_t firmware_update_upload(httpd_req_t *req)
{
#if CONFIG_FRAME_OTA_ENABLE
#if !CONFIG_SECURE_SIGNED_ON_UPDATE || !CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#error "Wireless updates require signature verification and rollback"
#endif
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    esp_ota_img_states_t state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!target || target == running ||
        (esp_ota_get_state_partition(running, &state) == ESP_OK &&
         state == ESP_OTA_IMG_PENDING_VERIFY))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Firmware is not ready for updates");
    if (!req->content_len || req->content_len > target->size)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Image does not fit the OTA slot");
    if (!settings_update_begin())
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Settings trial or restart in progress");
    char *buffer = malloc(4096);
    esp_ota_handle_t handle = 0;
    esp_err_t err = buffer ? esp_ota_begin(target, req->content_len, &handle) : ESP_ERR_NO_MEM;
    bool active = err == ESP_OK;
    size_t received = 0;
    int64_t deadline = esp_timer_get_time() + 120000000;
    while (err == ESP_OK && received < req->content_len) {
        if (esp_timer_get_time() >= deadline) {
            err = ESP_ERR_TIMEOUT;
            break;
        }
        size_t count = req->content_len - received;
        if (count > 4096)
            count = 4096;
        int n = httpd_req_recv(req, buffer, count);
        if (n <= 0) {
            err = ESP_FAIL;
            break;
        }
        err = esp_ota_write(handle, buffer, n);
        received += n;
    }
    free(buffer);
    if (err == ESP_OK) {
        /* IDF verifies the image AND its signature before selecting it for boot.
         * esp_ota_end releases the handle even when verification fails. */
        err = esp_ota_end(handle);
        active = false;
    }
    if (active)
        esp_ota_abort(handle);
    if (err == ESP_OK)
        err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
        settings_update_end();
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Incomplete, invalid or untrusted firmware");
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t sent = httpd_resp_send(req, "{\"ok\":true,\"restart\":true}", HTTPD_RESP_USE_STRLEN);
    /* Keep settings reserved until reboot, even if the response socket failed. */
    atomic_store(&restart_requested, true);
    return sent;
#else
    return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Signed firmware updates are disabled");
#endif
}
