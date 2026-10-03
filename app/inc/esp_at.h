/**
 * @file        esp_at.h
 * @brief       ESP8266 (ESP-AT firmware) UART transport
 *
 * @info        Single-owner API: only the MQTT link task calls into it.
 *              RX bytes arrive through IRQ_ID_UART_RX(UART_ESP) into a
 *              stream buffer; lines are framed by at_parse.c. Lines that are
 *              not the reply to the command in flight (WIFI DISCONNECT,
 *              +MQTTSUBRECV, …) go to the URC handler, also on the owner task.
 */

#ifndef APP_ESP_AT_H_
#define APP_ESP_AT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "at_parse.h"

typedef enum
{
    ESP_AT_OK = 0,      /* "OK"                                    */
    ESP_AT_ERROR,       /* "ERROR" or "FAIL"                       */
    ESP_AT_TIMEOUT,     /* no final result in time                 */
    ESP_AT_IO,          /* UART not ready / transmit failed / overflow */
} esp_at_result_t;

/** Called for every line that is not the final result of a command. */
typedef void (*esp_at_urc_cb_t)(at_line_t kind, const char *line, size_t len);

/** Called every ~100 ms while blocked waiting on the ESP (watchdog kick). */
typedef void (*esp_at_idle_cb_t)(void);

/**
 * @brief Claim the reset pin (released), the RX stream and the RX IRQ.
 * @note  Call from app_main() before the scheduler starts.
 */
int32_t esp_at_init(void);

void esp_at_set_urc_handler(esp_at_urc_cb_t cb);
void esp_at_set_idle_hook(esp_at_idle_cb_t cb);

/** True once the OS UART driver for UART_ESP is up (uart_mgmt, after boot). */
bool esp_at_uart_ready(void);

/**
 * @brief Pulse the ESP reset line and wait for "ready".
 * @return true if "ready" was seen within @p timeout_ms. Without a wired
 *         reset line this times out; the caller then falls back to AT+RST.
 */
bool esp_at_hw_reset(uint32_t timeout_ms);

/** Wait for a line of @p kind (URCs still dispatched). */
bool esp_at_wait_for(at_line_t kind, uint32_t timeout_ms);

/**
 * @brief Send one AT command (printf-style, CRLF appended) and wait for its
 *        final result. Intermediate lines go to the URC handler.
 */
esp_at_result_t esp_at_cmd(uint32_t timeout_ms, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/** Pump RX for up to @p timeout_ms, dispatching every line as a URC. */
void esp_at_poll(uint32_t timeout_ms);

/** Number of RX bytes lost to a full stream buffer since boot. */
uint32_t esp_at_rx_overruns(void);

#endif /* APP_ESP_AT_H_ */
