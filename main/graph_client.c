#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_event.h"
#include "tcpip_adapter.h"
#include "esp_tls.h"
#include "esp_http_client.h"
#include "graph_client.h"
#include "cJSON.h"
#include "wifi.h"
#include "uri_encode.h"

#define MAX_HTTP_RECV_BUFFER 4096
#define MAX_HTTP_TX_BUFFER 2048
static const char *TAG = "graph-client";

static esp_http_client_handle_t AUTH_CLIENT = NULL;
static esp_http_client_handle_t GRAPH_CLIENT = NULL;

static char *URL_FORMAT = NULL;

static const char *TENANT_ID = CONFIG_AAD_TENANT_ID;
static const char *CLIENT_ID = CONFIG_AAD_CLIENT_ID;
static char *DEVICE_CODE = NULL;

static const char *NVS_REFRESH_TOKEN_KEY = "refresh_token";
static const char *NVS_ACCESS_TOKEN_KEY = "access_token";

static nvs_handle_t NVS_HANDLE;

static const char *GRANT_TYPE = "urn:ietf:params:oauth:grant-type:device_code";
static const char *SCOPE = "presence.read offline_access";


static esp_err_t nvs_graphapi_open()
{
    ESP_LOGD(TAG, "Opening Non-Volatile Storage (NVS) handle... ");
    esp_err_t err = nvs_open("graphapi", NVS_READWRITE, &NVS_HANDLE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
        return err;
    } else {
        ESP_LOGD(TAG, "NVS open");
        return ESP_OK;
    }
}

static void nvs_graphapi_close()
{
    nvs_close(NVS_HANDLE);
}

esp_err_t _graph_client_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // Write out data
                ESP_LOGD(TAG, "%.*s", evt->data_len, (char*)evt->data);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            break;
    }
    return ESP_OK;
}

static esp_err_t init_auth_client()
{
    ESP_LOGD(TAG, "initializing auth http client");

    // set up auth url format
    asprintf(&URL_FORMAT, "https://login.microsoftonline.com/%s/oauth2/v2.0/%%s", TENANT_ID);
    
    esp_err_t err = ESP_OK;

    esp_http_client_config_t config = {
        .url = URL_FORMAT,
        .event_handler = _graph_client_event_handler,
        .buffer_size = MAX_HTTP_RECV_BUFFER
    };
    AUTH_CLIENT = esp_http_client_init(&config);
    err = esp_http_client_set_header(AUTH_CLIENT, "Content-Type", "application/x-www-form-urlencoded");
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "could not initialize http client");
        return err;
    }

    err = esp_http_client_set_method(AUTH_CLIENT, HTTP_METHOD_POST);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "could not initialize http client");
        return err;
    }

    return ESP_OK;
}

static esp_err_t init_aad_auth_flow()
{
    ESP_LOGD(TAG, "initializing Azure AD auth flow");

    esp_err_t err = ESP_OK;

    // TODO: Verify if refresh token exists in spiffy, if so, try to fetch new access token
    // If successful, store new refresh token, otherwise continue

    // update with correct path
    char *url = NULL;
    asprintf(&url, URL_FORMAT, "devicecode");
    esp_http_client_set_url(AUTH_CLIENT, url);

    // create payload
    char *data = NULL;
    char *scope_encoded = malloc(sizeof(char)*strlen(SCOPE)*2);
    uri_encode(SCOPE, strlen(SCOPE), scope_encoded);
    asprintf(&data, "client_id=%s&scope=%s", CLIENT_ID, scope_encoded);
    free(scope_encoded);
    ESP_LOGD(TAG, "Content of payload: %s", data);

    if((err = esp_http_client_set_post_field(AUTH_CLIENT, data, strlen(data)) != ESP_OK))
    {
        ESP_LOGE(TAG, "Failed to set payload: %s", esp_err_to_name(err));
        free(url);
        free(data);
        return err;
    }

    while((err = esp_http_client_perform(AUTH_CLIENT)) == ESP_ERR_HTTP_EAGAIN)
    {
        ESP_LOGI(TAG, "perform return ESP_ERR_HTTP_EAGAIN, retrying again..");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to execute POST method: %s", esp_err_to_name(err));
        free(url);
        free(data);
        return err;
    }
    ESP_LOGD(TAG, "esp_http_client_perform: complete");

    
    char *buffer = malloc(MAX_HTTP_RECV_BUFFER + 1);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Cannot malloc http receive buffer");
        free(buffer);
        free(url);
        free(data);
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "buffer malloc: complete");

    // fetch response data
    int content_length =  esp_http_client_get_content_length(AUTH_CLIENT);
    if(content_length > MAX_HTTP_RECV_BUFFER) {
        ESP_LOGE(TAG, "content length of %d is larger than the available buffer", content_length);
        free(buffer);
        free(url);
        free(data);
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "esp_http_client_fetch_headers: complete");

    int read_len;
    read_len = esp_http_client_read(AUTH_CLIENT, buffer, content_length);
    if (read_len <= 0) {
        free(buffer);
        free(url);
        free(data);
        ESP_LOGE(TAG, "Error read data");
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "esp_http_client_read: complete");
    buffer[read_len] = 0;
    ESP_LOGD(TAG, "read_len = %d", read_len);
    ESP_LOGI(TAG, "read response: %s", buffer);
    
    // parse data
    cJSON *root = cJSON_Parse(buffer);
    char *message = cJSON_GetObjectItem(root,"message")->valuestring;
    DEVICE_CODE = cJSON_GetObjectItem(root, "device_code")->valuestring;
    ESP_LOGI(TAG, "%s", message);
    ESP_LOGD(TAG, "cJSON_Parse: complete");

    // cleanup
    free(root);
    free(buffer);
    free(url);
    free(data);

    return ESP_OK;
}

static esp_err_t fetch_token(char *refresh_token)
{
    wifi_wait_connected();

    esp_err_t err = ESP_OK;

    // TODO: Verify if refresh token exists in spiffy, if so, try to fetch new access token
    // If successful, store new refresh token, otherwise continue

    // set correct path
    char *url = NULL;
    asprintf(&url, URL_FORMAT, "token");
    esp_http_client_set_url(AUTH_CLIENT, url);
    ESP_LOGD(TAG, "new url: %s", url);
    free(url);

    // create payload
    char *data = NULL;
    if(refresh_token == NULL) {
        char *grant_type_encoded = malloc(sizeof(char)*strlen(GRANT_TYPE)*2);
        uri_encode(GRANT_TYPE, strlen(GRANT_TYPE), grant_type_encoded);
        asprintf(&data, "grant_type=%s&client_id=%s&device_code=%s", grant_type_encoded, CLIENT_ID, DEVICE_CODE);
        free(grant_type_encoded);
    } else {
        asprintf(&data, "grant_type=refresh_token&client_id=%s&refresh_token=%s", CLIENT_ID, refresh_token);
    }
    ESP_LOGD(TAG, "content of payload: %s", data);

    err = esp_http_client_set_post_field(AUTH_CLIENT, data, strlen(data));
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "could not set post field for token fetch");
        return err;
    }
    ESP_LOGD(TAG, "esp_http_client_set_post_field: complete");

    int status_code = 0;
    while(status_code != 200)
    {
        if(status_code != 0)
        {
            vTaskDelay(15000 / portTICK_PERIOD_MS);
        }
        ESP_LOGD(TAG, "executing method call...");
        while((err = esp_http_client_perform(AUTH_CLIENT)) == ESP_ERR_HTTP_EAGAIN)
        {
            ESP_LOGI(TAG, "perform return ESP_ERR_HTTP_EAGAIN, retrying again..");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        if(err != ESP_OK)
        {
            ESP_LOGE(TAG, "unable to execute method: %s", esp_err_to_name(err));
            free(data);
            return err;
        }
        free(data);
        status_code = esp_http_client_get_status_code(AUTH_CLIENT);
        ESP_LOGD(TAG, "response status code: %d", status_code);
    }

    // fetch response
    char *buffer = malloc(MAX_HTTP_RECV_BUFFER + 1);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Cannot malloc http receive buffer");
        return ESP_FAIL;
    }

    int content_length =  esp_http_client_get_content_length(AUTH_CLIENT);
    if(content_length > MAX_HTTP_RECV_BUFFER) {
        ESP_LOGE(TAG, "content length of %d is larger than the available buffer", content_length);
        free(buffer);
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "esp_http_client_get_content_length: complete");

    int read_len;
    read_len = esp_http_client_read(AUTH_CLIENT, buffer, content_length);
    if (read_len <= 0) {
        free(buffer);
        ESP_LOGE(TAG, "Error read data");
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "esp_http_client_read: %d bytes, complete", read_len);
    ESP_LOGD(TAG, "response: %s", buffer);

    cJSON *root = cJSON_Parse(buffer);
    char *at = cJSON_GetObjectItem(root, "access_token")->valuestring;
    char *rt = cJSON_GetObjectItem(root, "refresh_token")->valuestring;
    ESP_LOGI(TAG, "access token: %s", at);
    ESP_LOGI(TAG, "refresh token: %s", rt);

    // persist tokens for later use
    nvs_graphapi_open();
    if((err = nvs_set_str(NVS_HANDLE, NVS_REFRESH_TOKEN_KEY, rt)) != ESP_OK) {
        ESP_LOGE(TAG, "unable to update refresh token in nvs");
    }
    if(err == ESP_OK)
    {
        char *authorization_header_value = malloc(strlen(at)+8);
        asprintf(&authorization_header_value, "Bearer %s", at);
        if((err = nvs_set_str(NVS_HANDLE, NVS_ACCESS_TOKEN_KEY, authorization_header_value)) != ESP_OK) {
            ESP_LOGE(TAG, "unable to update access token in nvs");
        }
        free(authorization_header_value);
    }
    if(err == ESP_OK)
    {
        if((err = nvs_commit(NVS_HANDLE)) != ESP_OK) {
            ESP_LOGE(TAG, "unable to persist changes into nvs");
        }
    }
    nvs_graphapi_close();    

    // cleanup
    cJSON_Delete(root);
    free(buffer);

    return err;
}

static esp_err_t refresh_access_token()
{
    esp_err_t err = ESP_OK;

    ESP_LOGI(TAG, "looking for persisted refresh token in NVS");
    ESP_ERROR_CHECK(nvs_graphapi_open());

    size_t required_size;
    nvs_get_str(NVS_HANDLE, NVS_REFRESH_TOKEN_KEY, NULL, &required_size);
    char* refresh_token = malloc(required_size);
    err = nvs_get_str(NVS_HANDLE, NVS_REFRESH_TOKEN_KEY, refresh_token, &required_size);

    nvs_graphapi_close();

    switch (err) {
        case ESP_OK:
            ESP_LOGI(TAG, "persisted refresh token found, trying to refresh access token.");
            ESP_LOGD(TAG, "refresh token value: %s", refresh_token);
            err = fetch_token(refresh_token);
            break;
        case ESP_ERR_NVS_NOT_FOUND:
            ESP_LOGI(TAG, "value is not initialized");
            break;
        default:
            ESP_LOGI(TAG, "error (%s) reading nvs!", esp_err_to_name(err));
            break;
    }
    free(refresh_token);
    return err;
}

static void graph_client_set_bearer_token()
{
    ESP_ERROR_CHECK(nvs_graphapi_open());
    esp_err_t err;

    size_t required_size;
    nvs_get_str(NVS_HANDLE, NVS_ACCESS_TOKEN_KEY, NULL, &required_size);
    char* authorization_header = malloc(required_size);
    err = nvs_get_str(NVS_HANDLE, NVS_ACCESS_TOKEN_KEY, authorization_header, &required_size);

    nvs_graphapi_close();

    switch (err) {
        case ESP_OK:
            ESP_LOGI(TAG, "persisted authorization header found: %s", authorization_header);
            break;
        case ESP_ERR_NVS_NOT_FOUND:
            ESP_LOGE(TAG, "value is not initialized, authorization header not set.");
            break;
        default:
            ESP_LOGE(TAG, "error (%s) reading nvs! authorization header not set.", esp_err_to_name(err));
            break;
    }

    err = esp_http_client_set_header(GRAPH_CLIENT, "authorization", authorization_header);
}

static esp_err_t init_graph_client()
{
    ESP_LOGD(TAG, "initializing graph API http client");
    
    esp_err_t err = ESP_OK;

    esp_http_client_config_t config = {
        .url = "https://graph.microsoft.com/beta/me/presence",
        .event_handler = _graph_client_event_handler,
        .buffer_size = MAX_HTTP_RECV_BUFFER,
        .buffer_size_tx = MAX_HTTP_TX_BUFFER,
        .method = HTTP_METHOD_GET
    };
    GRAPH_CLIENT = esp_http_client_init(&config);
    esp_http_client_set_header(GRAPH_CLIENT, "user-agent", "daddy-status/0.3.0");
    esp_http_client_set_header(GRAPH_CLIENT, "accept", "*/*");
    graph_client_set_bearer_token();

    err = esp_http_client_set_method(GRAPH_CLIENT, HTTP_METHOD_GET);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "could not initialize http client");
        return err;
    }

    return ESP_OK;
}

void poll_presence_task(QueueHandle_t *evtQueueHandle)
{
    esp_err_t err;
    while((err = init_graph_client()) != ESP_OK) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    for(;;)
    {
        wifi_wait_connected();
        vTaskDelay(5000 / portTICK_PERIOD_MS);

        while((err = esp_http_client_perform(GRAPH_CLIENT)) == ESP_ERR_HTTP_EAGAIN)
        {
            ESP_LOGI(TAG, "perform return ESP_ERR_HTTP_EAGAIN, retrying..");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
            continue;
        }

        int status_code = esp_http_client_get_status_code(GRAPH_CLIENT);
        int content_length = esp_http_client_get_content_length(GRAPH_CLIENT);

        if(status_code == 401 || status_code == 403)
        {
            ESP_LOGI(TAG, "received status code %d, refreshing access token to see if possible.", status_code);
            ESP_ERROR_CHECK(refresh_access_token());
            graph_client_set_bearer_token();
            continue;
        }

        if(status_code >= 500)
        {
            ESP_LOGE(TAG, "received status code %d, waiting an extra minute and trying again", status_code);
            vTaskDelay(60000 / portTICK_PERIOD_MS);
            continue;
        }

        if (status_code != 200)
        {
            ESP_LOGE(TAG, "received status code %d, will not continue..", status_code);
            vTaskDelete(NULL);
            return;
        }

        // fetch response
        char *buffer = malloc(256 + 1);
        int read_len;
        read_len = esp_http_client_read(GRAPH_CLIENT, buffer, content_length);
        if (read_len <= 0) {
            free(buffer);
            ESP_LOGE(TAG, "Error read data");
            continue;
        }
        ESP_LOGD(TAG, "esp_http_client_read: %d bytes, complete", read_len);
        ESP_LOGD(TAG, "response: %s", buffer);
        
        cJSON *root = cJSON_Parse(buffer);
        char *presence_activity = cJSON_GetObjectItem(root,"activity")->valuestring;
        ESP_LOGI(TAG, "presence: %s", presence_activity);

        uint8_t presence;
        //evt = malloc(sizeof(teams_presence_event_t));
        if (strcmp(presence_activity, "InACall") == 0 ||
            strcmp(presence_activity, "InAConferenceCall") == 0 ||
            strcmp(presence_activity, "InAMeeting") == 0) {
                presence = PRESENCE_IN_CALL;
        } else if(strcmp(presence_activity, "Presenting") == 0) {
            presence = PRESENCE_IN_VIDEO_CALL;
        } else {
            presence = PRESENCE_AVAILABLE;
        }
        xQueueSendFromISR(evtQueueHandle, (void*) &presence, NULL);

        cJSON_Delete(root);
        free(buffer);
    }
}

static esp_err_t refresh_token()
{
    esp_err_t err;
    while((err = init_auth_client()) != ESP_OK) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    
    // try to refresh using existing token
    err = refresh_access_token();
    while(err != ESP_OK)
    {
        // token not available, initiate auth flow
        err = init_aad_auth_flow();
        if(err != ESP_OK) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        err = fetch_token(NULL);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

   
    // TODO: Poll for new token when user has accepted

    // TODO: Store access token and refresh token in spiffy

    // TODO: Set timer and callback to fetch new access token and store the new refresh

    // esp_http_client_close(AUTH_CLIENT);
    // esp_http_client_cleanup(AUTH_CLIENT);

    return ESP_OK;
}

esp_err_t graph_client_init(QueueHandle_t *evtQueueHandle)
{
    ESP_LOGI(TAG, "initializing...");
    refresh_token();
    xTaskCreate(&poll_presence_task, "poll_presence_task", 15375, evtQueueHandle, 5, NULL);
    return ESP_OK;
}