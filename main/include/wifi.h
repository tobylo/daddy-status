#ifndef DADDY_WIFI_H
#define DADDY_WIFI_H
#include <stdbool.h>
#include <stdint.h>
#define WIFI_EVENT_HISTORY_SIZE 8

typedef enum {
    WIFI_DIAG_DISCONNECTED,
    WIFI_DIAG_RETRY_STARTED,
    WIFI_DIAG_RETRY_WAITING,
    WIFI_DIAG_TIMEOUT,
    WIFI_DIAG_CONNECTED,
    WIFI_DIAG_RECOVERY_AP_ENABLED,
    WIFI_DIAG_RECOVERY_AP_DISABLED,
} wifi_diag_event_type_t;

typedef struct {
    wifi_diag_event_type_t type;
    int64_t at_us;
    uint8_t reason;
    int8_t rssi;
    uint32_t retry_count;
} wifi_diag_event_t;

typedef struct {
    bool has_signal;
    int8_t rssi;
    bool has_disconnect;
    uint8_t last_disconnect_reason;
    uint32_t retry_count;
    int64_t next_retry_at_us;
    bool recovery_ap;
    uint32_t event_count;
    wifi_diag_event_t events[WIFI_EVENT_HISTORY_SIZE];
} wifi_diagnostics_t;

void wifi_init(void);
void wifi_wait_connected(void);
bool wifi_is_connected(void);
void wifi_diagnostics_snapshot(wifi_diagnostics_t *out);
#endif
