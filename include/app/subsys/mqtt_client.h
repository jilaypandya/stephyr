#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include "stdint.h"

typedef int (*mqtt_client_listener_t)(const uint8_t *topic, const char *payload);

/**
 *
 * @return
 */
void mqtt_client_attach_listener(mqtt_client_listener_t listener);

#endif
