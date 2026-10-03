/*
 * test_at_parse.c — host unit tests for app/src/at_parse.c
 *
 * Build and run from the project root:  make test
 */

#include <stdio.h>
#include <string.h>

#include "at_parse.h"

static int s_fail;
static int s_run;

#define CHECK(cond)                                                     \
    do {                                                                \
        s_run++;                                                        \
        if (!(cond)) {                                                  \
            s_fail++;                                                   \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
        }                                                               \
    } while (0)

/* Feed a byte stream; collect up to 16 lines. */
typedef struct
{
    char   line[16][AT_LINE_MAX + 1];
    size_t len[16];
    int    n;
} lines_t;

static void feed(at_lineasm_t *a, const char *s, size_t n, lines_t *out)
{
    for (size_t i = 0; i < n; i++)
    {
        if (at_lineasm_feed(a, (uint8_t)s[i]) && (out->n < 16))
        {
            memcpy(out->line[out->n], a->buf, a->len + 1U);
            out->len[out->n] = a->len;
            out->n++;
        }
    }
}

static void test_assembler(void)
{
    at_lineasm_t a;
    lines_t l;

    /* Typical AT exchange, with blank lines and an echo. */
    at_lineasm_reset(&a);
    memset(&l, 0, sizeof(l));
    const char *t1 = "AT+CWJAP=\"x\",\"y\"\r\nWIFI CONNECTED\r\nWIFI GOT IP\r\n\r\nOK\r\n";
    feed(&a, t1, strlen(t1), &l);
    CHECK(l.n == 4);
    CHECK(strcmp(l.line[1], "WIFI CONNECTED") == 0);
    CHECK(at_classify(l.line[2], l.len[2]) == AT_LINE_WIFI_GOT_IP);
    CHECK(at_classify(l.line[3], l.len[3]) == AT_LINE_OK);

    /* SUBRECV whose payload contains CR LF: must stay one line. */
    at_lineasm_reset(&a);
    memset(&l, 0, sizeof(l));
    const char t2[] = "+MQTTSUBRECV:0,\"nps/d/relay/1/set\",13,{\"on\":\r\ntrue}\r\nOK\r\n";
    feed(&a, t2, sizeof(t2) - 1U, &l);
    CHECK(l.n == 2);
    CHECK(at_classify(l.line[0], l.len[0]) == AT_LINE_MQTT_SUBRECV);
    {
        const char *topic; size_t tl; const uint8_t *d; size_t dl;
        CHECK(at_parse_subrecv(l.line[0], l.len[0], &topic, &tl, &d, &dl) == 0);
        CHECK(tl == 17 && memcmp(topic, "nps/d/relay/1/set", 17) == 0);
        CHECK(dl == 13 && memcmp(d, "{\"on\":\r\ntrue}", 13) == 0);
    }
    CHECK(strcmp(l.line[1], "OK") == 0);

    /* Payload ending in CR: the extra CR before CRLF belongs to the data. */
    at_lineasm_reset(&a);
    memset(&l, 0, sizeof(l));
    const char t3[] = "+MQTTSUBRECV:0,\"t\",3,ab\r\r\n";
    feed(&a, t3, sizeof(t3) - 1U, &l);
    CHECK(l.n == 1);
    {
        const char *topic; size_t tl; const uint8_t *d; size_t dl;
        CHECK(at_parse_subrecv(l.line[0], l.len[0], &topic, &tl, &d, &dl) == 0);
        CHECK(dl == 3 && memcmp(d, "ab\r", 3) == 0);
    }

    /* Overlong line is dropped whole; the next line is intact. */
    at_lineasm_reset(&a);
    memset(&l, 0, sizeof(l));
    char big[AT_LINE_MAX + 50];
    memset(big, 'x', sizeof(big));
    feed(&a, big, sizeof(big), &l);
    feed(&a, "\r\nready\r\n", 9, &l);
    CHECK(l.n == 1);
    CHECK(at_classify(l.line[0], l.len[0]) == AT_LINE_READY);

    /* Boot noise (74880-baud ROM output seen at 115200) then ready. */
    at_lineasm_reset(&a);
    memset(&l, 0, sizeof(l));
    const char t4[] = "\x8f\xff\x00l\x9c\x9e|\r\nready\r\n";
    feed(&a, t4, sizeof(t4) - 1U, &l);
    CHECK(l.n == 2);
    CHECK(at_classify(l.line[1], l.len[1]) == AT_LINE_READY);
}

static void test_classify(void)
{
    CHECK(at_classify("ERROR", 5) == AT_LINE_ERROR);
    CHECK(at_classify("FAIL", 4) == AT_LINE_FAIL);
    CHECK(at_classify("WIFI DISCONNECT", 15) == AT_LINE_WIFI_DISCONNECT);
    CHECK(at_classify("+MQTTCONNECTED:0,3,\"h\",\"8883\",\"\",0", 34) == AT_LINE_MQTT_CONNECTED);
    CHECK(at_classify("+MQTTDISCONNECTED:0", 19) == AT_LINE_MQTT_DISCONNECTED);
    CHECK(at_classify("OKAY", 4) == AT_LINE_OTHER);
    CHECK(at_classify("+CWJAP:1", 8) == AT_LINE_OTHER);
    CHECK(at_classify("", 0) == AT_LINE_OTHER);
}

static void test_subrecv(void)
{
    const char *topic; size_t tl; const uint8_t *d; size_t dl;
    const char *ok = "+MQTTSUBRECV:0,\"a/b\",11,{\"on\":true}";

    CHECK(at_parse_subrecv(ok, strlen(ok), &topic, &tl, &d, &dl) == 0);
    CHECK(tl == 3 && dl == 11);

    const char *zero = "+MQTTSUBRECV:0,\"a\",0,";
    CHECK(at_parse_subrecv(zero, strlen(zero), &topic, &tl, &d, &dl) == 0 && dl == 0);

    const char *short_ = "+MQTTSUBRECV:0,\"a/b\",12,{\"on\":true}";
    CHECK(at_parse_subrecv(short_, strlen(short_), &topic, &tl, &d, &dl) == -1);
    const char *noq = "+MQTTSUBRECV:0,a/b,3,abc";
    CHECK(at_parse_subrecv(noq, strlen(noq), &topic, &tl, &d, &dl) == -1);
    const char *nolen = "+MQTTSUBRECV:0,\"a\",,x";
    CHECK(at_parse_subrecv(nolen, strlen(nolen), &topic, &tl, &d, &dl) == -1);
    const char *huge = "+MQTTSUBRECV:0,\"a\",9999999,x";
    CHECK(at_parse_subrecv(huge, strlen(huge), &topic, &tl, &d, &dl) == -1);
    CHECK(at_parse_subrecv("OK", 2, &topic, &tl, &d, &dl) == -1);
}

static void test_escape(void)
{
    char out[64];

    CHECK(at_escape(out, sizeof(out), "{\"on\":true,\"src\":\"app\"}") > 0);
    CHECK(strcmp(out, "{\\\"on\\\":true\\,\\\"src\\\":\\\"app\\\"}") == 0);

    CHECK(at_escape(out, sizeof(out), "p\\w,d") == 7);
    CHECK(strcmp(out, "p\\\\w\\,d") == 0);

    CHECK(at_escape(out, sizeof(out), "") == 0 && out[0] == '\0');
    CHECK(at_escape(out, 4, "abc") == 3);
    CHECK(at_escape(out, 3, "abc") == -1);
    CHECK(at_escape(out, 4, "a\"b") == -1);
}

int main(void)
{
    test_assembler();
    test_classify();
    test_subrecv();
    test_escape();

    printf("at_parse: %d/%d checks passed\n", s_run - s_fail, s_run);
    return (s_fail == 0) ? 0 : 1;
}
