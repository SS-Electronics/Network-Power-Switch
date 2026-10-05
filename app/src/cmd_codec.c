/**
 * @file        cmd_codec.c
 * @brief       MQTT topic / JSON payload codec for the relay protocol
 *
 * @info        The JSON reader handles exactly what a set command needs: one
 *              flat object of string keys and scalar values. Nested values are
 *              rejected rather than skipped — nothing in the contract uses
 *              them, and a smaller grammar is a smaller attack surface on a
 *              device that switches mains power.
 */

#include <stdio.h>
#include <string.h>

#include "cmd_codec.h"

/* ── Topic ──────────────────────────────────────────────────────────────── */

uint8_t cmd_codec_parse_set_topic(const char *topic, const char *prefix,
                                  uint8_t max_channel)
{
    static const char mid[] = "/relay/";
    static const char tail[] = "/set";
    size_t plen = strlen(prefix);

    if ((topic == NULL) || (strncmp(topic, prefix, plen) != 0))
    {
        return 0;
    }
    topic += plen;

    if (strncmp(topic, mid, sizeof(mid) - 1U) != 0)
    {
        return 0;
    }
    topic += sizeof(mid) - 1U;

    /* 1..3 digits, no sign, no leading zero. */
    unsigned n = 0;
    unsigned digits = 0;
    while ((*topic >= '0') && (*topic <= '9') && (digits < 3U))
    {
        if ((digits == 0U) && (*topic == '0'))
        {
            return 0;
        }
        n = (n * 10U) + (unsigned)(*topic - '0');
        topic++;
        digits++;
    }

    if ((digits == 0U) || (n > max_channel) || (strcmp(topic, tail) != 0))
    {
        return 0;
    }

    return (uint8_t)n;
}

/* ── JSON payload ───────────────────────────────────────────────────────── */

typedef struct
{
    const uint8_t *p;
    const uint8_t *end;
} json_cur_t;

static void json_ws(json_cur_t *c)
{
    while ((c->p < c->end) &&
           ((*c->p == ' ') || (*c->p == '\t') || (*c->p == '\r') || (*c->p == '\n')))
    {
        c->p++;
    }
}

static bool json_take(json_cur_t *c, char ch)
{
    json_ws(c);
    if ((c->p < c->end) && (*c->p == (uint8_t)ch))
    {
        c->p++;
        return true;
    }
    return false;
}

static bool json_literal(json_cur_t *c, const char *lit)
{
    size_t n = strlen(lit);
    if (((size_t)(c->end - c->p) >= n) && (memcmp(c->p, lit, n) == 0))
    {
        c->p += n;
        return true;
    }
    return false;
}

/* Scan a string (cursor on the opening quote). Escapes are skipped, not
 * decoded: keys are compared raw, so "on" is not "on" — acceptable. */
static bool json_string(json_cur_t *c, const uint8_t **s, size_t *len)
{
    if ((c->p >= c->end) || (*c->p != '"'))
    {
        return false;
    }
    c->p++;
    *s = c->p;

    while (c->p < c->end)
    {
        if (*c->p == '\\')
        {
            c->p += 2;
            continue;
        }
        if (*c->p == '"')
        {
            *len = (size_t)(c->p - *s);
            c->p++;
            return true;
        }
        if (*c->p < 0x20U)
        {
            return false;
        }
        c->p++;
    }
    return false;
}

/* Skip a scalar value: string, number, true/false/null. */
static bool json_skip_scalar(json_cur_t *c)
{
    const uint8_t *s;
    size_t len;

    if (c->p >= c->end)
    {
        return false;
    }
    if (*c->p == '"')
    {
        return json_string(c, &s, &len);
    }
    if (json_literal(c, "true") || json_literal(c, "false") || json_literal(c, "null"))
    {
        return true;
    }

    const uint8_t *start = c->p;
    while ((c->p < c->end) &&
           (((*c->p >= '0') && (*c->p <= '9')) || (*c->p == '-') || (*c->p == '+') ||
            (*c->p == '.') || (*c->p == 'e') || (*c->p == 'E')))
    {
        c->p++;
    }
    return c->p != start;
}

int cmd_codec_parse_set_payload(const uint8_t *buf, size_t len, bool *on)
{
    json_cur_t c = { buf, buf + len };
    bool found = false;
    bool value = false;

    if ((buf == NULL) || (on == NULL) || (len == 0U) || (len > CMD_CODEC_MAX_PAYLOAD))
    {
        return -1;
    }

    if (!json_take(&c, '{'))
    {
        return -1;
    }

    if (!json_take(&c, '}'))
    {
        for (;;)
        {
            const uint8_t *key;
            size_t klen;

            json_ws(&c);
            if (!json_string(&c, &key, &klen) || !json_take(&c, ':'))
            {
                return -1;
            }
            json_ws(&c);

            if ((klen == 2U) && (memcmp(key, "on", 2) == 0))
            {
                if (json_literal(&c, "true"))
                {
                    value = true;
                }
                else if (json_literal(&c, "false"))
                {
                    value = false;
                }
                else
                {
                    return -1;
                }
                found = true;
            }
            else if (!json_skip_scalar(&c))
            {
                return -1;
            }

            if (json_take(&c, '}'))
            {
                break;
            }
            if (!json_take(&c, ','))
            {
                return -1;
            }
        }
    }

    /* Nothing but whitespace (or a C string terminator) may follow. */
    json_ws(&c);
    if ((c.p < c.end) && !((*c.p == '\0') && (c.p + 1 == c.end)))
    {
        return -1;
    }

    if (!found)
    {
        return -1;
    }

    *on = value;
    return 0;
}

/* ── Formatting ─────────────────────────────────────────────────────────── */

static int fit(int n, size_t cap)
{
    return ((n < 0) || ((size_t)n >= cap)) ? -1 : n;
}

int cmd_codec_format_state_topic(char *out, size_t cap, const char *prefix,
                                 uint8_t channel)
{
    return fit(snprintf(out, cap, "%s/relay/%u/state", prefix, (unsigned)channel), cap);
}

int cmd_codec_format_state(char *out, size_t cap, bool on, const char *src)
{
    return fit(snprintf(out, cap, "{\"on\":%s,\"src\":\"%s\"}",
                        on ? "true" : "false", src), cap);
}

/*
 * Both status payloads carry the SESSION id of the connection that produced
 * them, and the two are built as a pair at every connect.
 *
 * WHY: the last will is registered with the broker at CONNECT and published
 * later, whenever the broker decides that session died. If Wi-Fi vanishes
 * without a TCP FIN the broker only notices at keep-alive timeout (~45 s),
 * by which point this device has usually already reconnected and published
 * "online". The stale will then lands AFTER it and the server is left showing
 * a connected device as offline, with nothing to correct it until the next
 * reconnect.
 *
 * Tagging both with the same session lets the server discard a will whose
 * session is not the one currently online. A counter would be ambiguous
 * across a reboot (it restarts, so an old will could outrank a new session);
 * an id that simply has to be DIFFERENT from the previous one avoids that.
 */
int cmd_codec_format_online(char *out, size_t cap, const char *fw,
                            uint8_t relay_count, uint32_t session)
{
    return fit(snprintf(out, cap,
                        "{\"online\":true,\"fw\":\"%s\",\"relays\":%u,\"ses\":\"%08lx\"}",
                        fw, (unsigned)relay_count, (unsigned long)session), cap);
}

int cmd_codec_format_offline(char *out, size_t cap, uint32_t session)
{
    return fit(snprintf(out, cap, "{\"online\":false,\"ses\":\"%08lx\"}",
                        (unsigned long)session), cap);
}
