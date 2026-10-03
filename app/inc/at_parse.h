/**
 * @file        at_parse.h
 * @brief       ESP-AT response parsing: line assembly, classification,
 *              +MQTTSUBRECV decoding, AT string-parameter escaping
 *
 * @info        Pure C, no OS dependency: unit-tested on the host
 *              (app/test/test_at_parse.c, `make test`). Targets Espressif
 *              ESP-AT >= 2.2 for ESP8266 (the firmware with MQTT AT commands).
 */

#ifndef APP_AT_PARSE_H_
#define APP_AT_PARSE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Longest line kept; longer lines are discarded whole. */
#define AT_LINE_MAX     320U

typedef enum
{
    AT_LINE_OTHER = 0,          /* echo, info, +CWJAP:…, version text, noise */
    AT_LINE_OK,                 /* "OK"                                      */
    AT_LINE_ERROR,              /* "ERROR"                                   */
    AT_LINE_FAIL,               /* "FAIL"                                    */
    AT_LINE_READY,              /* "ready" — ESP finished booting            */
    AT_LINE_WIFI_CONNECTED,     /* "WIFI CONNECTED"                          */
    AT_LINE_WIFI_GOT_IP,        /* "WIFI GOT IP"                             */
    AT_LINE_WIFI_DISCONNECT,    /* "WIFI DISCONNECT"                         */
    AT_LINE_MQTT_CONNECTED,     /* "+MQTTCONNECTED:…"                        */
    AT_LINE_MQTT_DISCONNECTED,  /* "+MQTTDISCONNECTED:…"                     */
    AT_LINE_MQTT_SUBRECV,       /* "+MQTTSUBRECV:…"                          */
} at_line_t;

/** Byte-wise line assembler (CRLF framing, length-aware for SUBRECV). */
typedef struct
{
    char    buf[AT_LINE_MAX + 1];
    size_t  len;
    bool    overflow;           /* current line exceeded AT_LINE_MAX: drop it */
    bool    delivered;          /* buf holds a completed line                 */
} at_lineasm_t;

void at_lineasm_reset(at_lineasm_t *a);

/**
 * @brief Feed one received byte.
 * @return true when a complete, non-empty line is in a->buf (NUL-terminated,
 *         CR/LF stripped, length a->len). The caller consumes it before the
 *         next feed; the assembler restarts automatically.
 * @note   A +MQTTSUBRECV payload may itself contain CR/LF: the declared
 *         length is honoured, so such a line only completes after the whole
 *         payload has arrived.
 */
bool at_lineasm_feed(at_lineasm_t *a, uint8_t b);

/** Classify a complete line. */
at_line_t at_classify(const char *line, size_t len);

/**
 * @brief Decode +MQTTSUBRECV:<link>,"<topic>",<len>,<data>
 * @param[out] topic      points into @p line; NOT NUL-terminated
 * @param[out] data       points into @p line
 * @return 0 on success, -1 if malformed or the length does not match.
 */
int at_parse_subrecv(const char *line, size_t len,
                     const char **topic, size_t *topic_len,
                     const uint8_t **data, size_t *data_len);

/**
 * @brief Escape a string for use inside an AT "…" parameter: backslash
 *        before '"', ',' and '\\' (ESP-AT rule).
 * @return output length, or -1 if @p cap is too small (incl. NUL).
 */
int at_escape(char *out, size_t cap, const char *in);

#endif /* APP_AT_PARSE_H_ */
