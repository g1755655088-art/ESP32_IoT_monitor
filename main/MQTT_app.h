#ifndef MQTT_APP_H
#define MQTT_APP_H

#include <stdbool.h>

void mqtt_app_start(void);

bool mqtt_app_publish(
    const char *topic,
    const char *payload
);

#endif