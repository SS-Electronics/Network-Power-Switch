/**
 * @file        at_parse.c
 * @brief       ESP-AT response parsing (host-testable, no OS dependency)
 */

#include <string.h>

#include "at_parse.h"

#define SUBRECV_PREFIX  "+MQTTSUBRECV:"

/* ── +MQTTSUBRECV header ────────────────────────────────────────────────── */

/* Parse the header of a (possibly incomplete) SUBRECV line.
 * Returns 1 when the header is complete (sets *data_off and *declared),
 * 0 when more bytes are needed, -1 when it cannot be a valid header. */
static int subrecv_header(const char *s, size_t len, size_t *data_off, size_t *declared,
                          size_t *topic_off, size_t *topic_len)
{
    static const size_t plen = sizeof(SUBRECV_PREFIX) - 1U;
    size_t i = plen;

    if ((len < plen) || (memcmp(s, SUBRECV_PREFIX, plen) != 0))
    {
        return -1;
    }

    /* link id */
    if ((i >= len) || (s[i] < '0') || (s[i] > '9'))
    {
        return (i >= len) ? 0 : -1;
    }
    while ((i < len) && (s[i] >= '0') && (s[i] <= '9'))
    {
        i++;
    }
    if (i >= len) return 0;
    if (s[i++] != ',') return -1;

    /* "topic" — MQTT topics cannot contain '"' in practice; ESP-AT does
     * not escape it either, so the first closing quote ends the topic. */
    if (i >= len) return 0;
    if (s[i++] != '"') return -1;
    size_t t0 = i;
    while ((i < len) && (s[i] != '"'))
    {
        i++;
    }
    if (i >= len) return 0;
    size_t t1 = i++;
    if (i >= len) return 0;
    if (s[i++] != ',') return -1;

    /* length */
    size_t n = 0;
    size_t digits = 0;
    while ((i < len) && (s[i] >= '0') && (s[i] <= '9'))
    {
        if (digits++ >= 5U) return -1;
        n = (n * 10U) + (size_t)(s[i] - '0');
        i++;
    }
    if (i >= len) return 0;
    if ((digits == 0U) || (s[i++] != ',')) return -1;

    *data_off  = i;
    *declared  = n;
    *topic_off = t0;
    *topic_len = t1 - t0;
    return 1;
}

/* ── Line assembler ─────────────────────────────────────────────────────── */

void at_lineasm_reset(at_lineasm_t *a)
{
    a->len = 0;
    a->overflow = false;
    a->delivered = false;
    a->buf[0] = '\0';
}

bool at_lineasm_feed(at_lineasm_t *a, uint8_t b)
{
    /* First byte after a delivered line starts a new one. */
    if (a->delivered)
    {
        at_lineasm_reset(a);
    }

    if (b == '\n')
    {
        size_t len = a->len;
        size_t off, declared, toff, tlen;

        if (a->overflow)
        {
            at_lineasm_reset(a);
            return false;
        }

        /* A SUBRECV payload shorter than declared means this LF is data. */
        if (subrecv_header(a->buf, len, &off, &declared, &toff, &tlen) == 1)
        {
            size_t have = len - off;
            bool cr_terminated = (have > declared) && (a->buf[len - 1U] == '\r');

            if (!cr_terminated && (have < declared + 1U))
            {
                if (len >= AT_LINE_MAX)
                {
                    a->overflow = true;
                }
                else
                {
                    a->buf[a->len++] = (char)b;
                }
                return false;
            }
        }

        if ((len > 0U) && (a->buf[len - 1U] == '\r'))
        {
            len--;
        }
        a->buf[len] = '\0';
        a->len = len;

        if (len == 0U)
        {
            return false;
        }

        /* Caller reads buf/len now; the next feed starts a fresh line. */
        a->delivered = true;
        return true;
    }

    if (a->overflow)
    {
        return false;
    }
    if (a->len >= AT_LINE_MAX)
    {
        a->overflow = true;
        return false;
    }

    a->buf[a->len++] = (char)b;
    a->buf[a->len] = '\0';
    return false;
}

/* ── Classification ─────────────────────────────────────────────────────── */

static bool is(const char *line, size_t len, const char *lit)
{
    size_t n = strlen(lit);
    return (len == n) && (memcmp(line, lit, n) == 0);
}

static bool starts(const char *line, size_t len, const char *lit)
{
    size_t n = strlen(lit);
    return (len >= n) && (memcmp(line, lit, n) == 0);
}

at_line_t at_classify(const char *line, size_t len)
{
    if (is(line, len, "OK"))                   return AT_LINE_OK;
    if (is(line, len, "ERROR"))                return AT_LINE_ERROR;
    if (is(line, len, "FAIL"))                 return AT_LINE_FAIL;
    if (is(line, len, "ready"))                return AT_LINE_READY;
    if (is(line, len, "WIFI CONNECTED"))       return AT_LINE_WIFI_CONNECTED;
    if (is(line, len, "WIFI GOT IP"))          return AT_LINE_WIFI_GOT_IP;
    if (is(line, len, "WIFI DISCONNECT"))      return AT_LINE_WIFI_DISCONNECT;
    if (starts(line, len, "+MQTTCONNECTED:"))  return AT_LINE_MQTT_CONNECTED;
    if (starts(line, len, "+MQTTDISCONNECTED:")) return AT_LINE_MQTT_DISCONNECTED;
    if (starts(line, len, SUBRECV_PREFIX))     return AT_LINE_MQTT_SUBRECV;
    return AT_LINE_OTHER;
}

int at_parse_subrecv(const char *line, size_t len,
                     const char **topic, size_t *topic_len,
                     const uint8_t **data, size_t *data_len)
{
    size_t off, declared, toff, tlen;

    if (subrecv_header(line, len, &off, &declared, &toff, &tlen) != 1)
    {
        return -1;
    }
    if ((len - off) != declared)
    {
        return -1;
    }

    *topic     = &line[toff];
    *topic_len = tlen;
    *data      = (const uint8_t *)&line[off];
    *data_len  = declared;
    return 0;
}

/* ── Escaping ───────────────────────────────────────────────────────────── */

int at_escape(char *out, size_t cap, const char *in)
{
    size_t o = 0;

    for (; *in != '\0'; in++)
    {
        bool esc = (*in == '"') || (*in == ',') || (*in == '\\');
        size_t need = esc ? 2U : 1U;

        if ((o + need) >= cap)
        {
            return -1;
        }
        if (esc)
        {
            out[o++] = '\\';
        }
        out[o++] = *in;
    }

    if (o >= cap)
    {
        return -1;
    }
    out[o] = '\0';
    return (int)o;
}
