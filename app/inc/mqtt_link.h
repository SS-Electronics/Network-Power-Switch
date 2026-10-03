/**
 * @file        mqtt_link.h
 * @brief       MQTT-over-TLS session to the broker: commands in, state out
 *
 * @info        One task owns the session. lwIP MQTT/altcp callbacks run in
 *              the tcpip thread and only post events to it; every lwIP call
 *              the task makes is under LOCK_TCPIP_CORE(). Contract:
 *              docs/mqtt_api.md.
 */

#ifndef APP_MQTT_LINK_H_
#define APP_MQTT_LINK_H_

#include <stdint.h>

/**
 * @brief Create the MQTT task and hook the relay observer.
 * @note  Call from app_main() after relay_init() and net_service_start().
 * @return OS_ERR_NONE or a negative OS_ERR_* code.
 */
int32_t mqtt_link_start(void);

#endif /* APP_MQTT_LINK_H_ */
