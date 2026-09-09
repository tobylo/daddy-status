#include "../main/firmware_download.c"
#include <assert.h>
#include <stdio.h>

static esp_partition_t target = {.size = 8192};
static bool reserved, unavailable, fail_open, fail_init, fail_send, disconnect, oversized, slow;
static int initialized, cleaned, opened, http_status, remote_status;
static size_t sent, received;
static int64_t now;
static char requested_url[2048], error_message[256];
static const char *redirect_url;
static esp_http_client_config_t configuration;

const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *p)
{
    return unavailable ? NULL : &target;
}
bool settings_update_begin(void)
{
    if (reserved)
        return false;
    reserved = true;
    return true;
}
void settings_update_end(void)
{
    reserved = false;
}
int64_t esp_timer_get_time(void)
{
    return now;
}
esp_err_t esp_crt_bundle_attach(void *config)
{
    return ESP_OK;
}

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    ++initialized;
    configuration = *config;
    assert(config->crt_bundle_attach == esp_crt_bundle_attach && config->disable_auto_redirect);
    assert(config->timeout_ms == 10000 && config->buffer_size == 2048);
    snprintf(requested_url, sizeof(requested_url), "%s", config->url);
    return fail_init ? NULL : (void *)1;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key,
                                     const char *value)
{
    // Never forward the local control token or other credentials to GitHub.
    assert(!strcmp(key, "Accept") || !strcmp(key, "User-Agent"));
    return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int length)
{
    ++opened;
    assert(reserved);
    return fail_open ? ESP_FAIL : ESP_OK;
}
long long esp_http_client_fetch_headers(esp_http_client_handle_t client)
{
    if (redirect_url && opened == 1) {
        esp_http_client_event_t event = {.event_id = HTTP_EVENT_ON_HEADER,
                                         .header_key = "Location",
                                         .header_value = (char *)redirect_url,
                                         .user_data = configuration.user_data};
        return configuration.event_handler(&event) == ESP_OK ? 0 : -1;
    }
    return esp_http_client_get_content_length(client);
}
int esp_http_client_get_status_code(esp_http_client_handle_t client)
{
    return redirect_url && opened == 1 ? 302 : remote_status;
}
long long esp_http_client_get_content_length(esp_http_client_handle_t client)
{
    return oversized ? 8193 : 5000;
}
int esp_http_client_read(esp_http_client_handle_t client, char *buffer, int capacity)
{
    if (disconnect)
        return -1;
    if (slow)
        now += 61000000;
    int count = 5000 - received;
    if (count > 1024)
        count = 1024;
    assert(count <= capacity);
    memset(buffer, 0xa5, count);
    received += count;
    return count;
}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client)
{
    ++cleaned;
    return ESP_OK;
}
esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type)
{
    return ESP_OK;
}
esp_err_t httpd_resp_send_err(httpd_req_t *req, int status, const char *message)
{
    http_status = status;
    snprintf(error_message, sizeof(error_message), "%s", message);
    return ESP_FAIL;
}
esp_err_t httpd_resp_send_chunk(httpd_req_t *req, const char *buffer, int length)
{
    if (fail_send)
        return ESP_FAIL;
    sent += length;
    if (!length)
        http_status = 200;
    return ESP_OK;
}

static void reset(void)
{
    reserved = unavailable = fail_open = fail_init = fail_send = disconnect = oversized = slow =
        false;
    initialized = cleaned = opened = http_status = 0;
    sent = received = now = 0;
    redirect_url = NULL;
    remote_status = 200;
}

#if CONFIG_FRAME_OTA_ENABLE
static void successful_downloads(void)
{
    reset();
    assert(firmware_download(NULL, "123") == ESP_OK);
    assert(strstr(requested_url, "/tobylo/daddy-status/releases/assets/123"));
    assert(sent == 5000 && http_status == 200 && cleaned == 1 && !reserved);
    reset();
    redirect_url = "https://release-assets.githubusercontent.com/asset?signature=value";
    assert(firmware_download(NULL, "123") == ESP_OK);
    assert(!strcmp(requested_url, redirect_url));
    assert(sent == 5000 && initialized == 2 && cleaned == 2 && !reserved);
}

static void rejected_inputs(void)
{
    const char *invalid[] = {"",    "-1", "https://evil.test/", "1/../2", "123456789012345678901",
                             "12\n"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        reset();
        assert(firmware_download(NULL, invalid[i]) == ESP_FAIL);
        assert(http_status == 400 && !initialized && !reserved);
    }
    reset();
    reserved = true;
    assert(firmware_download(NULL, "123") == ESP_FAIL && reserved && !initialized);
    reset();
    unavailable = true;
    assert(firmware_download(NULL, "123") == ESP_FAIL && !initialized);
}

static void rejected_redirects(void)
{
    const char *urls[] = {"http://release-assets.githubusercontent.com/a", "https://evil.test/a",
                          "https://release-assets.githubusercontent.com.evil.test/a",
                          "https://release-assets.githubusercontent.com@evil.test/a"};
    for (unsigned i = 0; i < sizeof(urls) / sizeof(urls[0]); ++i) {
        reset();
        redirect_url = urls[i];
        assert(firmware_download(NULL, "123") == ESP_FAIL);
        assert(initialized == 1 && cleaned == 1 && !sent && !reserved);
    }
}

static void failed_transfers(void)
{
    bool *failures[] = {&fail_open, &fail_init, &fail_send, &disconnect, &oversized, &slow};
    for (unsigned i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
        reset();
        *failures[i] = true;
        assert(firmware_download(NULL, "123") == ESP_FAIL);
        assert(!reserved && http_status != 200);
        assert(cleaned == (fail_init ? 0 : 1));
    }
    reset();
    remote_status = 403;
    assert(firmware_download(NULL, "123") == ESP_FAIL);
    assert(strstr(error_message, "rate limit") && !sent && !reserved);
}
#endif

int main(void)
{
    reset();
#if CONFIG_FRAME_OTA_ENABLE
    successful_downloads();
    rejected_inputs();
    rejected_redirects();
    failed_transfers();
#else
    assert(firmware_download(NULL, "123") == ESP_FAIL);
    assert(http_status == 403 && !initialized);
#endif
    puts("GitHub streaming, TLS, redirect allowlist, limits and failure cleanup passed");
}
