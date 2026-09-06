#ifndef DADDY_WIFI_H
#define DADDY_WIFI_H
#include <stdbool.h>
#include <stdint.h>
void wifi_init(void);
void wifi_wait_connected(void);
bool wifi_is_connected(void);
void wifi_recovery_tick(int64_t now);
#endif
