/**
 * @file        mqtt_link.c
 * @brief       MQTT-over-TLS session to the broker: commands in, state out
 *
 * @info        Session lifecycle (task context):
 *
 *                IDLE ──connect──▶ CONNECTING ──CONNACK──▶ ONLINE
 *                  ▲                    │ timeout/refused       │ lost
 *                  └──── backoff ◀──────┴───────────────────────┘
 *
 *              On ONLINE: subscribe nps/<id>/relay/+/set (QoS 1), publish the
 *              retained online status, then every relay state (retained).
 *              The broker publishes the retained LWT {"online":false} when
 *              the session dies without a clean disconnect.
 *
 *              Events travel from tcpip-thread callbacks to the task as
 *              integers packed into the mailbox pointer: no allocation, and a
 *              session generation tag lets the task discard callbacks from an
 *              attempt it already abandoned.
 */

#include <string.h>

#include <os/kernel.h>
#include <os/kernel_syscall.h>
#include <services/gpio_mgmt.h>
#include <board/board_device_ids.h>
#include <safety/wdog.h>
#include <drivers/drv_eth.h>

#include "lwip/tcpip.h"
#include "lwip/ip_addr.h"
#include "lwip/altcp_tls.h"
#include "lwip/apps/mqtt.h"
#include "lwip/apps/mqtt_priv.h"
#include "mbedtls/ssl.h"

#include "conf_app.h"
#include "cmd_codec.h"
#include "relay.h"
#include "mqtt_link.h"

/* ── Events ─────────────────────────────────────────────────────────────── */

typedef enum
{
    EV_CONN_OK = 1,     /* CONNACK accepted                       */
    EV_CONN_LOST,       /* connect failed / refused / dropped     */
    EV_SUB_FAIL,        /* SUBACK refused (ACL) or timed out      */
    EV_CMD,             /* set command: ch, on                    */
    EV_WAKE,            /* relay changed: flush dirty states      */
} ev_type_t;

#define EV_PACK(t, gen, ch, val)                                          \
    ((void *)(uintptr_t)(((uint32_t)(t) << 24) | ((uint32_t)(gen) << 16) | \
                         ((uint32_t)(ch) << 8) | (uint32_t)(val)))
#define EV_TYPE(m)          ((uint8_t)((uintptr_t)(m) >> 24))
#define EV_GEN(m)           ((uint8_t)((uintptr_t)(m) >> 16))
#define EV_CH(m)            ((uint8_t)((uintptr_t)(m) >> 8))
#define EV_VAL(m)           ((uint8_t)(uintptr_t)(m))

#define MBOX_DEPTH          16U
#define TICK_MS             125U    /* LED blink + watchdog cadence */

typedef enum
{
    LINK_IDLE = 0,
    LINK_CONNECTING,
    LINK_ONLINE,
} link_state_t;

/* ── State ──────────────────────────────────────────────────────────────── */

static os_mbox_t                 s_mbox;
static mqtt_client_t            *s_client;
static struct altcp_tls_config  *s_tls;

/* Written by tcpip-thread callbacks, read by the task. */
static volatile mqtt_connection_status_t s_last_status;

/* Incoming publish reassembly (tcpip thread only). */
static uint8_t  s_in_ch;
static bool     s_in_drop;
static size_t   s_in_len;
static uint8_t  s_in_buf[CMD_CODEC_MAX_PAYLOAD];

/* Channels whose state still has to be published (bit n-1 = channel n). */
static volatile uint32_t    s_dirty;
static volatile relay_src_t s_src[NPS_RELAY_COUNT + 1];

/* tcpip liveness probe. */
static volatile uint32_t s_tcpip_seen_ms;
static volatile bool     s_probe_pending;

/* ── Helpers ────────────────────────────────────────────────────────────── */

static void post_from_tcpip(void *ev)
{
    if (os_mbox_trypost(s_mbox, ev) != OS_ERR_NONE)
    {
        printk("[mqtt] event queue full, dropped type %u\n", (unsigned)EV_TYPE(ev));
    }
}

/* Post only on change: the gpio_mgmt queue is 16 deep and shared with the
 * relay mirror LED, so a per-tick post could crowd that out. */
static void led(uint8_t id, bool on)
{
    static int8_t last_net = -1;
    static int8_t last_fault = -1;
    int8_t *last = (id == LED_NET) ? &last_net : &last_fault;

    if (*last != (int8_t)on)
    {
        if (gpio_mgmt_post(id, on ? GPIO_MGMT_CMD_SET : GPIO_MGMT_CMD_CLEAR, 0, 0) == OS_ERR_NONE)
        {
            *last = (int8_t)on;
        }
    }
}

static const char *status_name(mqtt_connection_status_t st)
{
    switch (st)
    {
        case MQTT_CONNECT_ACCEPTED:                 return "accepted";
        case MQTT_CONNECT_REFUSED_PROTOCOL_VERSION: return "refused: protocol";
        case MQTT_CONNECT_REFUSED_IDENTIFIER:       return "refused: client id";
        case MQTT_CONNECT_REFUSED_SERVER:           return "refused: server unavailable";
        case MQTT_CONNECT_REFUSED_USERNAME_PASS:    return "refused: bad username/password";
        case MQTT_CONNECT_REFUSED_NOT_AUTHORIZED_:  return "refused: not authorized";
        case MQTT_CONNECT_TIMEOUT:                  return "timeout";
        case MQTT_CONNECT_DISCONNECTED:
        default:                                    return "disconnected (TCP/TLS closed)";
    }
}

/* ── tcpip-thread callbacks ─────────────────────────────────────────────── */

static void on_connection(mqtt_client_t *client, void *arg, mqtt_connection_status_t st)
{
    (void)client;
    uint8_t gen = (uint8_t)(uintptr_t)arg;

    s_last_status = st;
    post_from_tcpip(EV_PACK((st == MQTT_CONNECT_ACCEPTED) ? EV_CONN_OK : EV_CONN_LOST,
                            gen, 0, 0));
}

static void on_subscribed(void *arg, err_t err)
{
    if (err != ERR_OK)
    {
        post_from_tcpip(EV_PACK(EV_SUB_FAIL, (uint8_t)(uintptr_t)arg, 0, 0));
    }
}

static void on_incoming_publish(void *arg, const char *topic, u32_t tot_len)
{
    (void)arg;

    s_in_ch   = cmd_codec_parse_set_topic(topic, NPS_TOPIC_PREFIX, NPS_RELAY_COUNT);
    s_in_len  = 0;
    s_in_drop = (s_in_ch == 0U) || (tot_len == 0U) || (tot_len > sizeof(s_in_buf));

    if (s_in_drop)
    {
        printk("[mqtt] ignored publish on %s (%lu B)\n", topic, (unsigned long)tot_len);
    }
}

static void on_incoming_data(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
    uint8_t gen = (uint8_t)(uintptr_t)arg;
    bool on;

    if (!s_in_drop)
    {
        if ((s_in_len + len) > sizeof(s_in_buf))
        {
            s_in_drop = true;
        }
        else
        {
            memcpy(&s_in_buf[s_in_len], data, len);
            s_in_len += len;
        }
    }

    if ((flags & MQTT_DATA_FLAG_LAST) == 0U || s_in_drop)
    {
        return;
    }

    if (cmd_codec_parse_set_payload(s_in_buf, s_in_len, &on) == 0)
    {
        post_from_tcpip(EV_PACK(EV_CMD, gen, s_in_ch, on ? 1U : 0U));
    }
    else
    {
        printk("[mqtt] bad payload for relay %u, expected {\"on\":true|false}\n",
               (unsigned)s_in_ch);
    }
}

static void on_tcpip_probe(void *arg)
{
    (void)arg;
    s_tcpip_seen_ms = os_uptime_ms();
    s_probe_pending = false;
}

/* Called by relay.c from whichever task switched the relay. */
static void on_relay_changed(uint8_t channel, bool on, relay_src_t src)
{
    (void)on;
    s_src[channel] = src;
    __disable_irq();
    s_dirty |= (1UL << (channel - 1U));
    __enable_irq();
    (void)os_mbox_trypost(s_mbox, EV_PACK(EV_WAKE, 0, 0, 0));
}

/* ── Session operations (task context) ──────────────────────────────────── */

static err_t link_connect(uint8_t gen)
{
    ip_addr_t broker;
    err_t err;

    struct mqtt_connect_client_info_t ci = {
        .client_id   = NPS_DEVICE_ID,
        .client_user = NPS_MQTT_USER,
        .client_pass = NPS_MQTT_PASS,
        .keep_alive  = NPS_MQTT_KEEPALIVE_S,
        .will_topic  = NPS_TOPIC_STATUS,
        .will_msg    = NPS_STATUS_OFFLINE_MSG,
        .will_qos    = 1,
        .will_retain = 1,
        .tls_config  = s_tls,
    };

    IP_ADDR4(&broker, NPS_BROKER_IP0, NPS_BROKER_IP1, NPS_BROKER_IP2, NPS_BROKER_IP3);

    LOCK_TCPIP_CORE();
    err = mqtt_client_connect(s_client, &broker, NPS_BROKER_PORT, on_connection,
                              (void *)(uintptr_t)gen, &ci);
    if (err == ERR_OK)
    {
        /* mqtt_client_connect() wipes the client, so these go after it. */
        mqtt_set_inpub_callback(s_client, on_incoming_publish, on_incoming_data,
                                (void *)(uintptr_t)gen);

        /* mbedTLS >= 3.6.3 refuses to verify a certificate without a
         * reference name. The handshake has not started yet: TCP is still
         * connecting and we hold the core lock. */
        mbedtls_ssl_context *ssl = (mbedtls_ssl_context *)altcp_tls_context(s_client->conn);
        if ((ssl == NULL) || (mbedtls_ssl_set_hostname(ssl, NPS_BROKER_HOST) != 0))
        {
            mqtt_disconnect(s_client);
            err = ERR_VAL;
        }
    }
    UNLOCK_TCPIP_CORE();

    return err;
}

static void link_drop(void)
{
    LOCK_TCPIP_CORE();
    mqtt_disconnect(s_client);
    UNLOCK_TCPIP_CORE();
}

static bool link_on_online(uint8_t gen)
{
    char msg[64];
    err_t err;
    int n = cmd_codec_format_online(msg, sizeof(msg), NPS_FW_VERSION, NPS_RELAY_COUNT);

    LOCK_TCPIP_CORE();
    err = mqtt_subscribe(s_client, NPS_TOPIC_SET_FILTER, 1, on_subscribed,
                         (void *)(uintptr_t)gen);
    if ((err == ERR_OK) && (n > 0))
    {
        err = mqtt_publish(s_client, NPS_TOPIC_STATUS, msg, (u16_t)n, 1, 1, NULL, NULL);
    }
    UNLOCK_TCPIP_CORE();

    if (err != ERR_OK)
    {
        printk("[mqtt] subscribe/status publish failed (err %d)\n", (int)err);
        return false;
    }

    /* Re-announce every channel so retained state is right after any gap. */
    __disable_irq();
    s_dirty = (1UL << NPS_RELAY_COUNT) - 1UL;
    __enable_irq();
    return true;
}

/* Publish every dirty channel; leaves a channel dirty if the ring is full. */
static void link_flush_states(void)
{
    char topic[64];
    char msg[48];

    for (uint8_t ch = 1; ch <= NPS_RELAY_COUNT; ch++)
    {
        uint32_t bit = 1UL << (ch - 1U);
        if ((s_dirty & bit) == 0U)
        {
            continue;
        }

        __disable_irq();
        s_dirty &= ~bit;
        __enable_irq();

        int tl = cmd_codec_format_state_topic(topic, sizeof(topic), NPS_TOPIC_PREFIX, ch);
        int ml = cmd_codec_format_state(msg, sizeof(msg), relay_get(ch),
                                        relay_src_name(s_src[ch]));
        if ((tl < 0) || (ml < 0))
        {
            continue;
        }

        LOCK_TCPIP_CORE();
        err_t err = mqtt_publish(s_client, topic, msg, (u16_t)ml, 1, 1, NULL, NULL);
        UNLOCK_TCPIP_CORE();

        if (err != ERR_OK)
        {
            __disable_irq();
            s_dirty |= bit;
            __enable_irq();
            return;
        }
    }
}

/* ── Liveness ───────────────────────────────────────────────────────────── */

static void liveness_tick(void)
{
    uint32_t now = os_uptime_ms();

    if (!s_probe_pending)
    {
        s_probe_pending = true;
        if (tcpip_try_callback(on_tcpip_probe, NULL) != ERR_OK)
        {
            s_probe_pending = false;
        }
    }

    /* Withhold the kick if tcpip is wedged: the IWDG then resets the board,
     * which is the only recovery from a hung network stack. */
    if ((now - s_tcpip_seen_ms) < NPS_TCPIP_STALL_MS)
    {
        wdog_task_kick(NPS_WDOG_SLOT_MQTT);
    }
}

/* ── Task ───────────────────────────────────────────────────────────────── */

static void mqtt_link_task(void *param)
{
    (void)param;

    link_state_t state       = LINK_IDLE;
    uint32_t     backoff_ms  = NPS_RECONNECT_MIN_MS;
    uint32_t     next_try_ms = 0;
    uint32_t     t_start_ms  = 0;
    uint8_t      gen         = 0;
    uint32_t     tick        = 0;
    bool         fault       = false;

    s_tcpip_seen_ms = os_uptime_ms();

    static const uint8_t ca[] = NPS_BROKER_CA_PEM;

    LOCK_TCPIP_CORE();
    s_client = mqtt_client_new();
    s_tls    = altcp_tls_create_config_client(ca, sizeof(ca));
    UNLOCK_TCPIP_CORE();

    if ((s_client == NULL) || (s_tls == NULL))
    {
        /* Bad CA PEM or out of lwIP heap: nothing to retry, the build is wrong.
         * Keep kicking so the relays stay controllable from the button. */
        printk("[mqtt] init failed: client=%p tls=%p (check NPS_BROKER_CA_PEM)\n",
               (void *)s_client, (void *)s_tls);
        led(LED_FAULT, true);
        for (;;)
        {
            liveness_tick();
            os_thread_delay(500);
        }
    }

    for (;;)
    {
        void *ev = NULL;
        uint32_t now;

        (void)os_mbox_fetch(s_mbox, &ev, TICK_MS);
        now = os_uptime_ms();
        tick++;

        liveness_tick();

        /* ── events ── */
        if (ev != NULL)
        {
            uint8_t type = EV_TYPE(ev);
            bool current = (EV_GEN(ev) == gen);

            if ((type == EV_CONN_OK) && current && (state == LINK_CONNECTING))
            {
                printk("[mqtt] online in %lu ms\n", (unsigned long)(now - t_start_ms));
                if (link_on_online(gen))
                {
                    state      = LINK_ONLINE;
                    backoff_ms = NPS_RECONNECT_MIN_MS;
                    fault      = false;
                    led(LED_FAULT, false);
                }
                else
                {
                    link_drop();
                    state = LINK_IDLE;
                }
            }
            else if (((type == EV_CONN_LOST) || (type == EV_SUB_FAIL)) && current &&
                     (state != LINK_IDLE))
            {
                mqtt_connection_status_t st = s_last_status;

                if (type == EV_SUB_FAIL)
                {
                    printk("[mqtt] subscribe refused: check broker ACL for %s\n",
                           NPS_TOPIC_SET_FILTER);
                    link_drop();
                    fault = true;
                }
                else
                {
                    printk("[mqtt] %s\n", status_name(st));
                    fault = (st == MQTT_CONNECT_REFUSED_USERNAME_PASS) ||
                            (st == MQTT_CONNECT_REFUSED_NOT_AUTHORIZED_) ||
                            (st == MQTT_CONNECT_REFUSED_IDENTIFIER);
                }
                led(LED_FAULT, fault);
                state = LINK_IDLE;
            }
            else if ((type == EV_CMD) && current && (state == LINK_ONLINE))
            {
                (void)relay_set(EV_CH(ev), EV_VAL(ev) != 0U, RELAY_SRC_APP);
            }
        }

        /* ── state actions ── */
        switch (state)
        {
            case LINK_IDLE:
                led(LED_NET, (tick & 7U) == 0U);        /* short blip: offline */

                if ((int32_t)(now - next_try_ms) < 0)
                {
                    break;
                }
                if (!drv_eth_link_is_up())
                {
                    next_try_ms = now + 1000U;
                    break;
                }

                gen++;
                t_start_ms = now;
                if (link_connect(gen) == ERR_OK)
                {
                    /* printk is gated off for the first ~2 s of boot, so the
                     * identity rides on this line rather than a boot banner. */
                    printk("[mqtt] connecting to %d.%d.%d.%d:%d as %s (fw %s, attempt %u)\n",
                           NPS_BROKER_IP0, NPS_BROKER_IP1, NPS_BROKER_IP2, NPS_BROKER_IP3,
                           NPS_BROKER_PORT, NPS_DEVICE_ID, NPS_FW_VERSION, (unsigned)gen);
                    state = LINK_CONNECTING;
                }
                else
                {
                    printk("[mqtt] connect could not start\n");
                }

                /* Schedule the next attempt now; it only fires from IDLE. */
                next_try_ms = now + backoff_ms;
                backoff_ms  = (backoff_ms * 2U > NPS_RECONNECT_MAX_MS)
                            ? NPS_RECONNECT_MAX_MS : backoff_ms * 2U;
                break;

            case LINK_CONNECTING:
                led(LED_NET, (tick & 1U) != 0U);        /* fast blink */
                if ((now - t_start_ms) > NPS_CONNECT_TIMEOUT_MS)
                {
                    printk("[mqtt] connect timed out after %lu ms\n",
                           (unsigned long)(now - t_start_ms));
                    link_drop();
                    state = LINK_IDLE;
                }
                break;

            case LINK_ONLINE:
                led(LED_NET, true);
                if (s_dirty != 0U)
                {
                    link_flush_states();
                }
                break;
        }
    }
}

int32_t mqtt_link_start(void)
{
    s_mbox = os_mbox_create(MBOX_DEPTH);
    if (s_mbox == NULL)
    {
        return OS_ERR_MEM_OF;
    }

    for (uint8_t ch = 0; ch <= NPS_RELAY_COUNT; ch++)
    {
        s_src[ch] = RELAY_SRC_BOOT;
    }

    relay_set_observer(on_relay_changed);

    int32_t id = os_thread_create(mqtt_link_task, "mqtt", NPS_MQTT_TASK_STACK,
                                  NPS_MQTT_TASK_PRIO, NULL);
    return (id < 0) ? id : OS_ERR_NONE;
}
