/**
 * @file        relay.h
 * @brief       4-channel active-LOW relay output driver
 *
 * @info        Channels are 1-based everywhere outside this module, matching
 *              the MQTT topics (nps/<id>/relay/<n>/...). Every state change is
 *              reported to one observer with the source that caused it, so the
 *              MQTT layer can publish it whatever triggered it.
 */

#ifndef APP_RELAY_H_
#define APP_RELAY_H_

#include <stdint.h>
#include <stdbool.h>

/** What caused a relay state change; published as "src" in the state message. */
typedef enum
{
    RELAY_SRC_BOOT = 0,     /**< power-up default (always OFF)             */
    RELAY_SRC_APP,          /**< MQTT set command                          */
    RELAY_SRC_BUTTON,       /**< local B1 button                           */
} relay_src_t;

/** Called from the context that changed the relay, after the pin moved. */
typedef void (*relay_observer_t)(uint8_t channel, bool on, relay_src_t src);

/**
 * @brief Drive every relay OFF and claim the pins.
 * @note  Call first thing in app_main(), before the scheduler starts: the
 *        pins are high-Z from reset until here, which the module's pull-ups
 *        already read as OFF, so there is no ON glitch.
 */
void relay_init(void);

/** Register the single change observer (NULL to clear). */
void relay_set_observer(relay_observer_t cb);

/**
 * @brief Switch one channel.
 * @param channel 1..NPS_RELAY_COUNT
 * @return true if the channel is valid (the state may already have matched)
 */
bool relay_set(uint8_t channel, bool on, relay_src_t src);

/** Invert one channel. @return false for an invalid channel. */
bool relay_toggle(uint8_t channel, relay_src_t src);

/** Current state of one channel (false for an invalid channel). */
bool relay_get(uint8_t channel);

/** True if any channel is ON. */
bool relay_any_on(void);

/** Short lowercase name of a source ("boot", "app", "button"). */
const char *relay_src_name(relay_src_t src);

#endif /* APP_RELAY_H_ */
