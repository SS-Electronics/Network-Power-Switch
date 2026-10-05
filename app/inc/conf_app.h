/**
 * @file        conf_app.h
 * @brief       Network-Power-Switch compile-time configuration
 *              (STM32F401CDU6 + ESP8266 running ESP-AT)
 *
 * @info        Everything that differs between units or deployments lives
 *              here or in conf_secrets.h (gitignored: Wi-Fi and MQTT
 *              credentials). The broker CA is NOT compiled into the MCU: TLS
 *              terminates in the ESP8266, which verifies the broker against
 *              the CA flashed into its mqtt_ca partition (tools/esp8266/).
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
#define NPS_FW_VERSION              "2.1.0"

/* ── Wi-Fi (station) ────────────────────────────────────────────────────── */

/**
 * Static IPv4 for the ESP8266 station. Comment out NPS_WIFI_STATIC_IP to use
 * DHCP from the access point instead.
 */
#define NPS_WIFI_STATIC_IP          "192.168.0.51"
#define NPS_WIFI_GATEWAY            "192.168.0.1"
#define NPS_WIFI_NETMASK            "255.255.255.0"

/** Give up on one AT+CWJAP attempt after this long. */
#define NPS_WIFI_JOIN_TIMEOUT_MS    20000U

/* ── Broker ─────────────────────────────────────────────────────────────── */

/**
 * Broker address as the ESP connects to it: an IP or, if the network has
 * DNS, a hostname. With an IP, the ESP8266's mbedTLS 2.x compares this
 * string against the certificate's dNSName SANs (it does not understand
 * iPAddress SANs), so the broker cert needs DNS:<this IP> as well.
 */
#define NPS_BROKER_HOST             "192.168.0.100"
#define NPS_BROKER_PORT             8883

/**
 * ESP-AT AT+MQTTUSERCFG scheme:
 *   1 = MQTT over TCP (plaintext — bench only)
 *   2 = MQTT over TLS, no certificate verification (do not ship)
 *   3 = MQTT over TLS, verify the broker against the ESP's mqtt_ca  ← default
 */
#define NPS_MQTT_SCHEME             3

/** MQTT keep-alive. The broker fires the LWT after ~1.5x this of silence. */
#define NPS_MQTT_KEEPALIVE_S        30

/** Reconnect backoff: doubles from MIN up to MAX after each failed attempt. */
#define NPS_RECONNECT_MIN_MS        2000U
#define NPS_RECONNECT_MAX_MS        60000U

/** AT+MQTTCONN (TCP + TLS handshake on the ESP8266 + CONNACK) budget. */
#define NPS_CONNECT_TIMEOUT_MS      30000U

/** Consecutive AT timeouts before the ESP8266 is hardware-reset. */
#define NPS_ESP_MAX_TIMEOUTS        3U

/* ── Topics ─────────────────────────────────────────────────────────────── */

#define NPS_TOPIC_ROOT              "nps"

/* nps/<id>/relay/<n>/set    app → device   {"on":true}
 * nps/<id>/relay/<n>/state  device → app   {"on":true,"src":"app"}  retained
 * nps/<id>/status           device → app   {"online":true,...}       retained, LWT */
#define NPS_TOPIC_PREFIX            NPS_TOPIC_ROOT "/" NPS_DEVICE_ID
#define NPS_TOPIC_SET_FILTER        NPS_TOPIC_PREFIX "/relay/+/set"
#define NPS_TOPIC_STATUS            NPS_TOPIC_PREFIX "/status"
/* The offline (will) payload is built per connect by cmd_codec_format_offline()
 * so it can carry that session's id; there is no fixed string any more. */

/* ── Relays ─────────────────────────────────────────────────────────────── */

#define NPS_RELAY_COUNT             4

/**
 * Relay inputs, all 5 V tolerant (FT) on STM32F401CDU6:
 *
 *   CH1  PB12      CH3  PB14
 *   CH2  PB13      CH4  PB15
 *
 * Driven open-drain, active-LOW: LOW sinks the module's opto LED (relay ON),
 * released = the module's own pull-up to its VCC (relay OFF). Open-drain is
 * what lets a 5 V-powered module turn fully off; a 3.3 V push-pull HIGH leaves
 * ~1.7 V across the opto and can hold it partly on.
 */
#define NPS_RELAY_PINS                    \
    {                                     \
        { GPIOB, GPIO_PIN_12 },           \
        { GPIOB, GPIO_PIN_13 },           \
        { GPIOB, GPIO_PIN_14 },           \
        { GPIOB, GPIO_PIN_15 },           \
    }

/* ── ESP8266 ────────────────────────────────────────────────────────────── */

/**
 * ESP8266 RST input (active LOW). Driven open-drain with the internal
 * pull-up: released = running. If RST is not wired, the hardware reset
 * simply times out and the firmware falls back to AT+RST.
 * UART: USART1, PA9 (MCU TX → ESP RX) / PA10 (MCU RX ← ESP TX), 115200 8N1,
 * declared as UART_ESP in board/nps_f401.xml.
 */
#define NPS_ESP_RST_PORT            GPIOB
#define NPS_ESP_RST_PIN             GPIO_PIN_0
#define NPS_ESP_RST_CLK_ENABLE()    __HAL_RCC_GPIOB_CLK_ENABLE()

/** MCU RX pin from the ESP's TX: read to tell "ESP silent" from "ESP off". */
#define NPS_ESP_RX_PORT             GPIOA
#define NPS_ESP_RX_PIN              GPIO_PIN_10

/* ── Local I/O ──────────────────────────────────────────────────────────── */

/** Channel (1-based) toggled by the KEY button (PA0, active-LOW). */
#define NPS_BUTTON_CHANNEL          1
#define NPS_BUTTON_DEBOUNCE_MS      50U
#define NPS_BUTTON_PRESSED_LEVEL    0U

/** PC13 on-board LED is active-LOW (the OS gpio path ignores active_state). */
#define NPS_LED_ACTIVE_LOW          1

/* ── Tasks (stack in words) ─────────────────────────────────────────────── */

#define NPS_MQTT_TASK_STACK         768
#define NPS_MQTT_TASK_PRIO          5
#define NPS_BUTTON_TASK_STACK       256
#define NPS_BUTTON_TASK_PRIO        3

/* ── Software watchdog slots (safety/wdog.h) ────────────────────────────── */

#define NPS_WDOG_SLOT_MQTT          3U
#define NPS_WDOG_SLOT_BUTTON        4U

#endif /* APP_CONF_APP_H_ */
