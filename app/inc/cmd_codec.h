/**
 * @file        cmd_codec.h
 * @brief       MQTT topic / JSON payload codec for the relay protocol
 *
 * @info        Pure C, no OS or lwIP dependency, so it is unit-tested on the
 *              host (app/test/test_cmd_codec.c, `make test`). Wire format is
 *              specified in docs/mqtt_api.md.
 */

#ifndef APP_CMD_CODEC_H_
#define APP_CMD_CODEC_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Largest set-command payload accepted; anything bigger is dropped. */
#define CMD_CODEC_MAX_PAYLOAD   128U

/**
 * @brief Match "<prefix>/relay/<n>/set" and extract n.
 * @return n in 1..max_channel, or 0 if the topic does not match.
 */
uint8_t cmd_codec_parse_set_topic(const char *topic, const char *prefix,
                                  uint8_t max_channel);

/**
 * @brief Parse a set payload: a JSON object whose "on" member is true/false.
 *        Unknown members are ignored so the server can add fields later.
 * @return 0 on success, -1 if malformed or "on" is missing / not a boolean.
 */
int cmd_codec_parse_set_payload(const uint8_t *buf, size_t len, bool *on);

/** "<prefix>/relay/<n>/state". @return length, or -1 if @p cap is too small. */
int cmd_codec_format_state_topic(char *out, size_t cap, const char *prefix,
                                 uint8_t channel);

/** {"on":true,"src":"app"}. @return length, or -1 if @p cap is too small. */
int cmd_codec_format_state(char *out, size_t cap, bool on, const char *src);

/** {"online":true,"fw":"1.0.0","relays":4}. @return length or -1. */
int cmd_codec_format_online(char *out, size_t cap, const char *fw,
                            uint8_t relay_count, uint32_t session);

/**
 * @brief   Format the offline (last will) status for @p session.
 * @note    Carries the same session id as the online status published on that
 *          connection, so the server can discard a will that belongs to a
 *          session which has already been replaced.
 */
int cmd_codec_format_offline(char *out, size_t cap, uint32_t session);

#endif /* APP_CMD_CODEC_H_ */
