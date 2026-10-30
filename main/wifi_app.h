#ifndef WIFI_APP_H
#define WIFI_APP_H

#include <stdbool.h>
#include <stdint.h>

// Wi-Fi Station 初始化函数
void wifi_init_sta(void);
bool wifi_wait_connected(uint32_t timeout_ms);

#endif // WIFI_APP_H
