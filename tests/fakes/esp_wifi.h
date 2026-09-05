#include "esp_err.h"
#include "sdkconfig.h"
#include <stdint.h>
typedef struct {
    int unused;
} wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
typedef struct {
    struct {
        char ssid[32];
        char password[64];
        unsigned listen_interval;
    } sta;
} wifi_config_t;
typedef struct {
    uint8_t channel;
} wifi_event_sta_connected_t;
typedef struct {
    uint8_t reason;
    int8_t rssi;
} wifi_event_sta_disconnected_t;
#define WIFI_STORAGE_RAM 0
#define WIFI_MODE_STA 0
#define WIFI_IF_STA 0
#define WIFI_PS_NONE 0
#define ESP_ERR_WIFI_CONN 0x3007
esp_err_t esp_wifi_init(const wifi_init_config_t *);
esp_err_t esp_wifi_set_storage(int);
esp_err_t esp_wifi_set_mode(int);
esp_err_t esp_wifi_set_config(int, const wifi_config_t *);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_wifi_set_ps(int);

esp_err_t esp_wifi_stop(void);
