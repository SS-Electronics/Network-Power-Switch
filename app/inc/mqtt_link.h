/**
 * @file        mqtt_link.h
 * @brief       MQTT-over-TLS session through the ESP8266: commands in, state out
 *
 * @info        One task owns the ESP8266 (esp_at.c) and walks it through
 *              reset → Wi-Fi join → MQTT connect → online. Contract:
 *              docs/mqtt_api.md.
 */

#ifndef APP_MQTT_LINK_H_
#define APP_MQTT_LINK_H_

#include <stdint.h>

/**
 * @brief Create the MQTT task and hook the relay observer.
 * @note  Call from app_main() after relay_init() and esp_at_init().
 * @return OS_ERR_NONE or a negative OS_ERR_* code.
 */
int32_t mqtt_link_start(void);

#endif /* APP_MQTT_LINK_H_ */
