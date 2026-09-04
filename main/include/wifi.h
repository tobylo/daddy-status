#ifndef DADDY_WIFI_H
#define DADDY_WIFI_H
#include <stdbool.h>
void wifi_init(void);
void wifi_wait_connected(void);
bool wifi_is_connected(void);
#endif
