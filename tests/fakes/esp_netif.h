#include "esp_err.h"
typedef struct {
    int unused;
} esp_netif_t;
typedef struct {
    unsigned addr;
} test_ip_t;
typedef struct {
    struct {
        test_ip_t ip;
    } ip_info;
} ip_event_got_ip_t;
#define IPSTR "%u"
#define IP2STR(p) (p)->addr
esp_err_t esp_netif_init(void);
esp_netif_t *esp_netif_create_default_wifi_sta(void);
esp_err_t esp_netif_set_hostname(esp_netif_t *, const char *);

esp_netif_t *esp_netif_create_default_wifi_ap(void);
