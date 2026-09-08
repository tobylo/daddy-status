#include "firmware_update.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "settings.h"
#include <stdlib.h>

/* All reboot/health state belongs to the main task, except the atomic restart request. */
#include <stdatomic.h>
static atomic_bool restart_requested;
static bool pending;
static int64_t started, connected_at;

static const int64_t HEALTH_STABLE_US = 30000000;
static const int64_t HEALTH_DEADLINE_US = 180000000;

void firmware_update_init(void)
{
    esp_ota_img_states_t state;
    pending = esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
              state == ESP_OTA_IMG_PENDING_VERIFY;
    started = esp_timer_get_time();
}

static void track_connection(bool connected, int64_t now)
{
    if (!connected)
        connected_at = 0;
    else if (!connected_at)
        connected_at = now;
}

static bool boot_healthy(int64_t now)
{
    return connected_at && now - connected_at >= HEALTH_STABLE_US && !settings_trial();
}

static void confirm_healthy_boot(void)
{
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        pending = false;
        ESP_LOGI("ota", "Firmware health check passed");
    }
}

void firmware_update_tick(bool connected, int64_t now)
{
    if (atomic_load(&restart_requested)) {
        esp_restart();
        return;
    }
    if (!pending)
        return;
    track_connection(connected, now);
    /* Exercise initialized services and the display loop, with 30s stable Wi-Fi.
     * Microsoft login/availability is deliberately not a boot health dependency. */
    if (boot_healthy(now))
        confirm_healthy_boot();
    if (pending && now - started >= HEALTH_DEADLINE_US) {
        ESP_LOGE("ota", "Firmware health check failed; rolling back");
        esp_ota_mark_app_invalid_rollback_and_reboot();
        esp_restart(); /* A failed rollback must not silently confirm this image. */
    }
}

#if CONFIG_FRAME_OTA_ENABLE
#if !CONFIG_SECURE_SIGNED_ON_UPDATE || !CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#error "Wireless updates require signature verification and rollback"
#endif
static const int64_t UPLOAD_TIMEOUT_US = 120000000;
enum { UPLOAD_BUFFER_SIZE = 4096 };

static bool update_slot_ready(const esp_partition_t *target)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!target || target == running)
        return false;
    esp_ota_img_states_t state;
    return esp_ota_get_state_partition(running, &state) != ESP_OK ||
           state != ESP_OTA_IMG_PENDING_VERIFY;
}

static esp_err_t receive_image(httpd_req_t *req, esp_ota_handle_t handle, char *buffer)
{
    size_t received = 0;
    int64_t deadline = esp_timer_get_time() + UPLOAD_TIMEOUT_US;
    while (received < req->content_len) {
        if (esp_timer_get_time() >= deadline)
            return ESP_ERR_TIMEOUT;
        size_t count = req->content_len - received;
        if (count > UPLOAD_BUFFER_SIZE)
            count = UPLOAD_BUFFER_SIZE;
        int n = httpd_req_recv(req, buffer, count);
        if (n <= 0)
            return ESP_FAIL;
        esp_err_t err = esp_ota_write(handle, buffer, n);
        if (err != ESP_OK)
            return err;
        received += n;
    }
    return ESP_OK;
}

static esp_err_t finalize_image(esp_ota_handle_t handle, const esp_partition_t *target,
                                esp_err_t received)
{
    if (received != ESP_OK) {
        esp_ota_abort(handle);
        return received;
    }
    /* IDF verifies the image AND its signature before selecting it for boot.
     * esp_ota_end releases the handle even when verification fails. */
    esp_err_t err = esp_ota_end(handle);
    if (err != ESP_OK)
        return err;
    return esp_ota_set_boot_partition(target);
}

static esp_err_t install_image(httpd_req_t *req, const esp_partition_t *target)
{
    char *buffer = malloc(UPLOAD_BUFFER_SIZE);
    if (!buffer)
        return ESP_ERR_NO_MEM;
    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(target, req->content_len, &handle);
    if (err != ESP_OK) {
        free(buffer);
        return err;
    }
    err = receive_image(req, handle, buffer);
    free(buffer);
    return finalize_image(handle, target, err);
}
#endif

esp_err_t firmware_update_upload(httpd_req_t *req)
{
#if CONFIG_FRAME_OTA_ENABLE
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!update_slot_ready(target))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Firmware is not ready for updates");
    if (!req->content_len || req->content_len > target->size)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Image does not fit the OTA slot");
    if (!settings_update_begin())
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Settings trial or restart in progress");
    esp_err_t err = install_image(req, target);
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
