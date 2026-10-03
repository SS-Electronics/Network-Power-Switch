/**
 * @file        conf_app.h
 * @brief       Network-Power-Switch compile-time configuration
 *
 * @info        Everything that differs between units or deployments lives
 *              here or in conf_secrets.h (gitignored: MQTT credentials and
 *              the broker CA). Network identity (IP / MAC) is in
 *              board/lwipopts.h next to the rest of the lwIP options.
 *
 *              The MQTT contract built from these values is specified in
 *              docs/mqtt_api.md — change both together.
 */

#ifndef APP_CONF_APP_H_
#define APP_CONF_APP_H_

#include "conf_secrets.h"

/* ── Device identity ────────────────────────────────────────────────────── */

/** Unique per unit. Used as the MQTT client id and in every topic. */
#define NPS_DEVICE_ID               "nps-0001"

/** Firmware version reported in the online status message. */
#define NPS_FW_VERSION              "1.0.0"

/* ── Broker ─────────────────────────────────────────────────────────────── */

/** Broker IPv4 address (no DNS on this build). */
#define NPS_BROKER_IP0              192
#define NPS_BROKER_IP1              168
#define NPS_BROKER_IP2              0
#define NPS_BROKER_IP3              100

/**
 * Name the broker certificate is verified against. With an IP-addressed
 * broker this is the IP as text, and the server certificate must carry it as
 * an iPAddress subjectAltName. mbedTLS refuses to verify without it.
 */
#define NPS_BROKER_HOST             "192.168.0.100"

#define NPS_BROKER_PORT             8883

/** MQTT keep-alive. The broker drops the session (and fires the LWT) after
 *  1.5x this without traffic, which is how the app learns we went offline. */
#define NPS_MQTT_KEEPALIVE_S        30

/** Reconnect backoff: doubles from MIN up to MAX after each failed attempt. */
#define NPS_RECONNECT_MIN_MS        2000U
#define NPS_RECONNECT_MAX_MS        60000U

/** Give up on a connect attempt (TCP + TLS + CONNACK) after this long. */
#define NPS_CONNECT_TIMEOUT_MS      20000U

/* ── Topics ─────────────────────────────────────────────────────────────── */

#define NPS_TOPIC_ROOT              "nps"

/* nps/<id>/relay/<n>/set    app → device   {"on":true}
 * nps/<id>/relay/<n>/state  device → app   {"on":true,"src":"app"}  retained
 * nps/<id>/status           device → app   {"online":true,...}       retained, LWT */
#define NPS_TOPIC_PREFIX            NPS_TOPIC_ROOT "/" NPS_DEVICE_ID
#define NPS_TOPIC_SET_FILTER        NPS_TOPIC_PREFIX "/relay/+/set"
#define NPS_TOPIC_STATUS            NPS_TOPIC_PREFIX "/status"
#define NPS_STATUS_OFFLINE_MSG      "{\"online\":false}"

/* ── Relays ─────────────────────────────────────────────────────────────── */

#define NPS_RELAY_COUNT             4

/**
 * Relay inputs on the Arduino header of the NUCLEO-H723ZG (all 5 V tolerant).
 *
 *   CH1  D2  PF15      CH3  D4  PF14
 *   CH2  D3  PE13      CH4  D5  PE11
 *
 * Driven open-drain, active-LOW: LOW sinks the module's opto LED (relay ON),
 * released = the module's own pull-up to its VCC (relay OFF). Open-drain is
 * what lets a 5 V-powered module turn fully off; a 3.3 V push-pull HIGH leaves
 * ~1.7 V across the opto and can hold it partly on.
 */
#define NPS_RELAY_PINS                    \
    {                                     \
        { GPIOF, GPIO_PIN_15 },           \
        { GPIOE, GPIO_PIN_13 },           \
        { GPIOF, GPIO_PIN_14 },           \
        { GPIOE, GPIO_PIN_11 },           \
    }

/** Channel (1-based) toggled by the B1 user button. */
#define NPS_BUTTON_CHANNEL          1
#define NPS_BUTTON_DEBOUNCE_MS      50U

/* ── Network bring-up guard ─────────────────────────────────────────────── */

/**
 * FreeRTOS-OS only brings the Ethernet up if the PHY links at boot. When it
 * does not (cable unplugged), the board reboots after this long to try again
 * — but only while every relay is OFF, so a load switched on with the button
 * is never dropped by the retry.
 */
#define NPS_NET_RETRY_REBOOT_MS     30000U

/* ── Tasks (stack in words) ─────────────────────────────────────────────── */

#define NPS_MQTT_TASK_STACK         1024
#define NPS_MQTT_TASK_PRIO          5
#define NPS_BUTTON_TASK_STACK       384
#define NPS_BUTTON_TASK_PRIO        3
#define NPS_NET_GUARD_STACK         256
#define NPS_NET_GUARD_PRIO          2

/* ── Software watchdog slots (safety/wdog.h) ────────────────────────────── */

#define NPS_WDOG_SLOT_MQTT          3U
#define NPS_WDOG_SLOT_BUTTON        4U

/**
 * If the tcpip thread has not run a probe callback for this long, the MQTT
 * task stops kicking its slot and the IWDG resets the board (relays OFF).
 * A TLS handshake runs inside tcpip, so this must exceed the slowest one.
 */
#define NPS_TCPIP_STALL_MS          8000U

#endif /* APP_CONF_APP_H_ */
