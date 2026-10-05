/*
 * test_cmd_codec.c — host unit tests for app/src/cmd_codec.c
 *
 * Build and run from the project root:  make test
 */

#include <stdio.h>
#include <string.h>

#include "cmd_codec.h"

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

static int payload(const char *s, bool *on)
{
    return cmd_codec_parse_set_payload((const uint8_t *)s, strlen(s), on);
}

static void test_topic(void)
{
    const char *pre = "nps/nps-0001";

    CHECK(cmd_codec_parse_set_topic("nps/nps-0001/relay/1/set", pre, 4) == 1);
    CHECK(cmd_codec_parse_set_topic("nps/nps-0001/relay/4/set", pre, 4) == 4);
    CHECK(cmd_codec_parse_set_topic("nps/nps-0001/relay/5/set", pre, 4) == 0);
    CHECK(cmd_codec_parse_set_topic("nps/nps-0001/relay/0/set", pre, 4) == 0);
    CHECK(cmd_codec_parse_set_topic("nps/nps-0001/relay/01/set", pre, 4) == 0);
    CHECK(cmd_codec_parse_set_topic("nps/nps-0001/relay//set", pre, 4) == 0);
    CHECK(cmd_codec_parse_set_topic("nps/nps-0001/relay/1/state", pre, 4) == 0);
    CHECK(cmd_codec_parse_set_topic("nps/nps-0001/relay/1/set/x", pre, 4) == 0);
    CHECK(cmd_codec_parse_set_topic("nps/nps-0002/relay/1/set", pre, 4) == 0);
    CHECK(cmd_codec_parse_set_topic("nps/nps-00011/relay/1/set", pre, 4) == 0);
    CHECK(cmd_codec_parse_set_topic("nps/nps-0001/relay/-1/set", pre, 4) == 0);
    CHECK(cmd_codec_parse_set_topic("nps/nps-0001/relay/9999/set", pre, 4) == 0);
    CHECK(cmd_codec_parse_set_topic(NULL, pre, 4) == 0);
}

static void test_payload(void)
{
    bool on = false;

    CHECK(payload("{\"on\":true}", &on) == 0 && on == true);
    CHECK(payload("{\"on\":false}", &on) == 0 && on == false);
    CHECK(payload("  { \"on\" : true }  \n", &on) == 0 && on == true);
    CHECK(payload("{\"id\":\"abc\",\"on\":false,\"ts\":1700000000}", &on) == 0 && on == false);
    CHECK(payload("{\"on\":true,\"note\":null,\"x\":-1.5e3}", &on) == 0 && on == true);
    CHECK(payload("{\"s\":\"a \\\"quoted\\\" }\",\"on\":true}", &on) == 0 && on == true);

    /* Last "on" wins, like most JSON parsers. */
    CHECK(payload("{\"on\":true,\"on\":false}", &on) == 0 && on == false);

    /* NUL terminator counted in the length is tolerated. */
    CHECK(cmd_codec_parse_set_payload((const uint8_t *)"{\"on\":true}", 12, &on) == 0);

    CHECK(payload("{}", &on) == -1);
    CHECK(payload("", &on) == -1);
    CHECK(payload("ON", &on) == -1);
    CHECK(payload("true", &on) == -1);
    CHECK(payload("{\"on\":1}", &on) == -1);
    CHECK(payload("{\"on\":\"true\"}", &on) == -1);
    CHECK(payload("{\"on\":tru}", &on) == -1);
    CHECK(payload("{\"on\":true", &on) == -1);
    CHECK(payload("{\"on\":true}}", &on) == -1);
    CHECK(payload("{\"on\":true} x", &on) == -1);
    CHECK(payload("{\"on\" true}", &on) == -1);
    CHECK(payload("{\"on\":true,}", &on) == -1);
    CHECK(payload("{\"x\":{\"on\":true}}", &on) == -1);
    CHECK(payload("{\"x\":[1],\"on\":true}", &on) == -1);
    CHECK(payload("{\"ON\":true}", &on) == -1);
    CHECK(payload("{\"o\\", &on) == -1);
    CHECK(cmd_codec_parse_set_payload(NULL, 4, &on) == -1);

    char big[CMD_CODEC_MAX_PAYLOAD + 16];
    memset(big, ' ', sizeof(big));
    memcpy(big, "{\"on\":true}", 11);
    CHECK(cmd_codec_parse_set_payload((const uint8_t *)big, sizeof(big), &on) == -1);
}

static void test_format(void)
{
    char buf[64];

    CHECK(cmd_codec_format_state_topic(buf, sizeof(buf), "nps/nps-0001", 3) > 0);
    CHECK(strcmp(buf, "nps/nps-0001/relay/3/state") == 0);

    CHECK(cmd_codec_format_state(buf, sizeof(buf), true, "button") > 0);
    CHECK(strcmp(buf, "{\"on\":true,\"src\":\"button\"}") == 0);

    /* Status and will are a PAIR: both carry the session id of the connection
     * that produced them, so the server can discard a will belonging to a
     * session that has already been replaced. */
    CHECK(cmd_codec_format_online(buf, sizeof(buf), "2.0.0", 4, 0x0a1b2c3dUL) > 0);
    CHECK(strcmp(buf,
                 "{\"online\":true,\"fw\":\"2.0.0\",\"relays\":4,\"ses\":\"0a1b2c3d\"}") == 0);

    CHECK(cmd_codec_format_offline(buf, sizeof(buf), 0x0a1b2c3dUL) > 0);
    CHECK(strcmp(buf, "{\"online\":false,\"ses\":\"0a1b2c3d\"}") == 0);

    /* Session is zero-padded to 8 hex digits so the server can compare it as a
     * plain string without worrying about leading zeros. */
    CHECK(cmd_codec_format_offline(buf, sizeof(buf), 0x1UL) > 0);
    CHECK(strcmp(buf, "{\"online\":false,\"ses\":\"00000001\"}") == 0);

    /* Truncation is reported, never silently emitted. */
    CHECK(cmd_codec_format_state(buf, 8, true, "app") == -1);
    CHECK(cmd_codec_format_online(buf, 8, "2.0.0", 4, 1UL) == -1);
    CHECK(cmd_codec_format_offline(buf, 8, 1UL) == -1);
}

int main(void)
{
    test_topic();
    test_payload();
    test_format();

    printf("%d/%d checks passed\n", s_run - s_fail, s_run);
    return (s_fail == 0) ? 0 : 1;
}
