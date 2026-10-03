/**
 * @file        mqtt_link.c
 * @brief       MQTT-over-TLS session through an ESP8266 (ESP-AT firmware)
 *
 * @info        The ESP8266 runs Espressif ESP-AT >= 2.2 and owns Wi-Fi, TCP,
 *              TLS and the MQTT client; this task drives it with AT commands:
 *
 *                ESP_INIT ──▶ WIFI ──▶ MQTT ──▶ ONLINE
 *                   ▲  3 AT timeouts │  wifi lost ▲  │ broker/wifi lost
 *                   └────────────────┴────────────┴──┘ (backoff)
 *
 *              ESP_INIT  reset (RST pin, else AT+RST), ATE0, AT+SYSSTORE=0
 *                        (no flash writes), station mode, static IP.
 *              WIFI      AT+CWJAP until "WIFI GOT IP".
 *              MQTT      AT+MQTTUSERCFG (TLS scheme, credentials),
 *                        AT+MQTTCONNCFG (keep-alive, retained LWT),
 *                        AT+MQTTCONN, AT+MQTTSUB, online status.
 *              ONLINE    publish dirty relay states; +MQTTSUBRECV → relay.
 *
 *              Unsolicited lines arrive through the esp_at URC handler on
 *              this same task, so there is no locking: the handler only sets
 *              flags or switches a relay (whose observer marks it dirty).
 */

#include <string.h>

#include <device.h>
#include <os/kernel.h>
#include <os/kernel_syscall.h>
#include <services/gpio_mgmt.h>
#include <board/board_device_ids.h>
#include <safety/wdog.h>

#include "conf_app.h"
#include "at_parse.h"
#include "cmd_codec.h"
#include "esp_at.h"
#include "relay.h"
#include "mqtt_link.h"

#define TICK_MS             100U
#define CMD_TIMEOUT_MS      2000U
#define PUB_TIMEOUT_MS      5000U

typedef enum
{
    ST_ESP_INIT = 0,
    ST_WIFI,
    ST_MQTT,
    ST_ONLINE,
} link_state_t;

/**
 * Link diagnostics, readable over SWD (the board's ST-LINK has no console):
 *   arm-none-eabi-nm nps.elf | grep g_nps_diag  → read with the programmer.
 */
typedef struct
{
    volatile uint32_t state;            /* link_state_t                     */
    volatile uint32_t wifi_up;
    volatile uint32_t mqtt_up;
    volatile uint32_t esp_resets;
    volatile uint32_t esp_responding;   /* last ESP_INIT got an AT reply    */
    volatile uint32_t wifi_joins;       /* AT+CWJAP attempts                */
    volatile uint32_t mqtt_connects;    /* AT+MQTTCONN attempts             */
    volatile uint32_t online_count;     /* successful sessions              */
    volatile uint32_t commands;         /* set commands applied             */
    volatile uint32_t last_result;      /* esp_at_result_t of last command  */
    volatile uint32_t rx_overruns;
    volatile uint32_t rx_line_low;      /* ESP TX idles LOW: ESP not running */
    volatile uint32_t esp_fw_unsupported; /* NonOS AT 1.x: no AT+MQTT* commands */
    char              esp_version[48];  /* first AT+GMR line                */
    char              last_line[64];    /* last unrecognised ESP line       */
} nps_diag_t;

nps_diag_t g_nps_diag;

/* ── State shared with the URC handler (same task) ──────────────────────── */

static bool     s_wifi_up;
static bool     s_mqtt_up;
static bool     s_esp_rebooted;
static bool     s_capture_version;
static uint32_t s_timeouts;

/* Channels whose state still has to be published (bit n-1 = channel n).
 * Written from the button task through the relay observer. */
static volatile uint32_t    s_dirty;
static volatile relay_src_t s_src[NPS_RELAY_COUNT + 1];

/* LED */
static link_state_t s_led_state;
static uint32_t     s_led_tick;

/* Escaped AT parameters, built once. */
static char s_ssid_esc[2 * sizeof(NPS_WIFI_SSID)];
static char s_wpass_esc[2 * sizeof(NPS_WIFI_PASS)];
static char s_user_esc[2 * sizeof(NPS_MQTT_USER)];
static char s_mpass_esc[2 * sizeof(NPS_MQTT_PASS)];
static char s_lwt_esc[2 * sizeof(NPS_STATUS_OFFLINE_MSG)];

/* ── LED + watchdog (idle hook: runs every ~100 ms, also inside AT waits) ── */

static void led(bool on)
{
    static int8_t last = -1;
#if NPS_LED_ACTIVE_LOW
    bool level = !on;
#else
    bool level = on;
#endif
    if (last != (int8_t)level)
    {
        if (gpio_mgmt_post(LED_NET, level ? GPIO_MGMT_CMD_SET : GPIO_MGMT_CMD_CLEAR,
                           0, 0) == OS_ERR_NONE)
        {
            last = (int8_t)level;
        }
    }
}

/* ESP_INIT: blip every 2 s · WIFI: 1 Hz · MQTT: 5 Hz · ONLINE: solid */
static void idle_tick(void)
{
    uint32_t t = s_led_tick++;

    wdog_task_kick(NPS_WDOG_SLOT_MQTT);

    switch (s_led_state)
    {
        case ST_ESP_INIT: led((t % 20U) == 0U);     break;
        case ST_WIFI:     led((t % 10U) < 5U);      break;
        case ST_MQTT:     led((t & 1U) != 0U);      break;
        case ST_ONLINE:   led(true);                break;
    }
}

static void set_state(link_state_t *st, link_state_t next)
{
    *st = next;
    s_led_state = next;
    g_nps_diag.state = (uint32_t)next;
}

/* ── URC handler ────────────────────────────────────────────────────────── */

static void copy_line(char *dst, size_t cap, const char *line, size_t len)
{
    size_t n = (len < cap - 1U) ? len : cap - 1U;
    memcpy(dst, line, n);
    dst[n] = '\0';
}

static void on_subrecv(const char *line, size_t len)
{
    const char *topic;
    const uint8_t *data;
    size_t tlen, dlen;
    char tbuf[64];
    bool on;

    if ((at_parse_subrecv(line, len, &topic, &tlen, &data, &dlen) != 0) ||
        (tlen >= sizeof(tbuf)))
    {
        printk("[mqtt] bad +MQTTSUBRECV frame\n");
        return;
    }
    memcpy(tbuf, topic, tlen);
    tbuf[tlen] = '\0';

    uint8_t ch = cmd_codec_parse_set_topic(tbuf, NPS_TOPIC_PREFIX, NPS_RELAY_COUNT);
    if (ch == 0U)
    {
        printk("[mqtt] ignored publish on %s\n", tbuf);
        return;
    }
    if (cmd_codec_parse_set_payload(data, dlen, &on) != 0)
    {
        printk("[mqtt] bad payload for relay %u\n", (unsigned)ch);
        return;
    }

    g_nps_diag.commands++;
    (void)relay_set(ch, on, RELAY_SRC_APP);
}

static void on_urc(at_line_t kind, const char *line, size_t len)
{
    switch (kind)
    {
        case AT_LINE_WIFI_GOT_IP:
            s_wifi_up = true;
            break;
        case AT_LINE_WIFI_DISCONNECT:
            s_wifi_up = false;
            s_mqtt_up = false;
            break;
        case AT_LINE_MQTT_CONNECTED:
            s_mqtt_up = true;
            break;
        case AT_LINE_MQTT_DISCONNECTED:
            s_mqtt_up = false;
            break;
        case AT_LINE_MQTT_SUBRECV:
            on_subrecv(line, len);
            break;
        case AT_LINE_READY:
            /* The ESP rebooted on its own (brown-out, crash). */
            s_esp_rebooted = true;
            break;
        case AT_LINE_OTHER:
            if (s_capture_version && (g_nps_diag.esp_version[0] == '\0'))
            {
                copy_line(g_nps_diag.esp_version, sizeof(g_nps_diag.esp_version), line, len);
            }
            /* Echo of our own command lines is off (ATE0); anything else is
             * worth keeping for diagnosis (+CWJAP:<reason>, error codes). */
            if ((len > 0U) && (strncmp(line, "AT", 2) != 0))
            {
                copy_line(g_nps_diag.last_line, sizeof(g_nps_diag.last_line), line, len);
            }
            break;
        default:
            break;
    }
}

/* Track consecutive AT timeouts; an ESP that stops answering gets reset. */
static esp_at_result_t track(esp_at_result_t r)
{
    g_nps_diag.last_result = (uint32_t)r;
    s_timeouts = ((r == ESP_AT_TIMEOUT) || (r == ESP_AT_IO)) ? s_timeouts + 1U : 0U;
    return r;
}

/* ── Relay observer ─────────────────────────────────────────────────────── */

static void on_relay_changed(uint8_t channel, bool on, relay_src_t src)
{
    (void)on;
    s_src[channel] = src;
    __disable_irq();
    s_dirty |= (1UL << (channel - 1U));
    __enable_irq();
}

/* ── Steps ──────────────────────────────────────────────────────────────── */

static bool step_esp_init(void)
{
    g_nps_diag.esp_resets++;
    g_nps_diag.esp_responding = 0;
    s_wifi_up = false;
    s_mqtt_up = false;
    s_esp_rebooted = false;
    s_timeouts = 0;

    if (!esp_at_hw_reset(4000))
    {
        /* RST not wired (or ESP already up): software reset instead. */
        (void)esp_at_cmd(1000, "AT+RST");
        (void)esp_at_wait_for(AT_LINE_READY, 5000);
    }
    s_esp_rebooted = false;

    esp_at_result_t r = ESP_AT_TIMEOUT;
    for (int i = 0; (i < 3) && (r != ESP_AT_OK); i++)
    {
        r = track(esp_at_cmd(1000, "ATE0"));
    }
    if (r != ESP_AT_OK)
    {
        /* A running ESP8266 holds its TX (our RX) HIGH when idle. LOW means
         * the chip is not running at all, not a protocol problem. */
        g_nps_diag.rx_line_low =
            (HAL_GPIO_ReadPin(NPS_ESP_RX_PORT, NPS_ESP_RX_PIN) == GPIO_PIN_RESET) ? 1U : 0U;
        printk(g_nps_diag.rx_line_low
               ? "[esp] RX line LOW: ESP not running (EN/CH_PD high? RST? 3V3 supply?)\n"
               : "[esp] no AT reply (TX/RX swapped? baud? ESP-AT firmware?)\n");
        return false;
    }
    g_nps_diag.rx_line_low = 0;
    g_nps_diag.esp_responding = 1;

    g_nps_diag.esp_version[0] = '\0';
    s_capture_version = true;
    (void)esp_at_cmd(CMD_TIMEOUT_MS, "AT+GMR");
    s_capture_version = false;
    printk("[esp] %s\n", g_nps_diag.esp_version);

    /* Keep Wi-Fi/MQTT settings in RAM only: no flash wear on every boot.
     * Older AT releases lack the command; harmless if it errors. */
    (void)esp_at_cmd(CMD_TIMEOUT_MS, "AT+SYSSTORE=0");

    if (track(esp_at_cmd(CMD_TIMEOUT_MS, "AT+CWMODE=1")) != ESP_AT_OK)
    {
        return false;
    }

#ifdef NPS_WIFI_STATIC_IP
    if (track(esp_at_cmd(CMD_TIMEOUT_MS, "AT+CIPSTA=\"%s\",\"%s\",\"%s\"",
                         NPS_WIFI_STATIC_IP, NPS_WIFI_GATEWAY, NPS_WIFI_NETMASK)) != ESP_AT_OK)
    {
        printk("[esp] static IP rejected\n");
        return false;
    }
#endif

    return true;
}

static bool step_wifi_join(void)
{
    g_nps_diag.wifi_joins++;
    printk("[wifi] joining \"%s\"\n", NPS_WIFI_SSID);

    esp_at_result_t r = track(esp_at_cmd(NPS_WIFI_JOIN_TIMEOUT_MS, "AT+CWJAP=\"%s\",\"%s\"",
                                         s_ssid_esc, s_wpass_esc));
    if (r == ESP_AT_OK)
    {
        s_wifi_up = true;
        printk("[wifi] connected\n");
        return true;
    }

    /* +CWJAP:<n> → 1 timeout, 2 wrong password, 3 AP not found, 4 failed */
    printk("[wifi] join failed (%s)\n", g_nps_diag.last_line);
    return false;
}

static bool step_mqtt_connect(void)
{
    char msg[64];
    char esc[96];

    /* NonOS AT 1.x has no MQTT commands: sending them only times out and
     * would reset the ESP in a loop. Keep Wi-Fi up and retry on backoff. */
    if (strncmp(g_nps_diag.esp_version, "AT version:1.", 13) == 0)
    {
        g_nps_diag.esp_fw_unsupported = 1;
        printk("[mqtt] ESP has %s: flash ESP-AT >= 2.2 for MQTT\n", g_nps_diag.esp_version);
        return false;
    }
    g_nps_diag.esp_fw_unsupported = 0;
    g_nps_diag.mqtt_connects++;

    /* Drop whatever the ESP still holds from a previous session. */
    (void)esp_at_cmd(CMD_TIMEOUT_MS, "AT+MQTTCLEAN=0");

    if (track(esp_at_cmd(CMD_TIMEOUT_MS, "AT+MQTTUSERCFG=0,%d,\"%s\",\"%s\",\"%s\",0,0,\"\"",
                         NPS_MQTT_SCHEME, NPS_DEVICE_ID, s_user_esc, s_mpass_esc)) != ESP_AT_OK)
    {
        printk("[mqtt] MQTTUSERCFG failed: ESP-AT >= 2.2 with MQTT support required\n");
        return false;
    }

    if (track(esp_at_cmd(CMD_TIMEOUT_MS, "AT+MQTTCONNCFG=0,%d,0,\"%s\",\"%s\",1,1",
                         NPS_MQTT_KEEPALIVE_S, NPS_TOPIC_STATUS, s_lwt_esc)) != ESP_AT_OK)
    {
        return false;
    }

    printk("[mqtt] connecting to %s:%d as %s (fw %s)\n", NPS_BROKER_HOST, NPS_BROKER_PORT,
           NPS_DEVICE_ID, NPS_FW_VERSION);
    uint32_t t0 = os_uptime_ms();

    if (track(esp_at_cmd(NPS_CONNECT_TIMEOUT_MS, "AT+MQTTCONN=0,\"%s\",%d,0",
                         NPS_BROKER_HOST, NPS_BROKER_PORT)) != ESP_AT_OK)
    {
        /* ESP-AT reports TLS/auth failures only as ERROR (+ an error code
         * line, kept in g_nps_diag.last_line). */
        printk("[mqtt] connect failed (%s)\n", g_nps_diag.last_line);
        return false;
    }
    s_mqtt_up = true;

    if (track(esp_at_cmd(PUB_TIMEOUT_MS, "AT+MQTTSUB=0,\"%s\",1", NPS_TOPIC_SET_FILTER)) != ESP_AT_OK)
    {
        printk("[mqtt] subscribe refused: check broker ACL for %s\n", NPS_TOPIC_SET_FILTER);
        return false;
    }

    int n = cmd_codec_format_online(msg, sizeof(msg), NPS_FW_VERSION, NPS_RELAY_COUNT);
    if ((n < 0) || (at_escape(esc, sizeof(esc), msg) < 0) ||
        (track(esp_at_cmd(PUB_TIMEOUT_MS, "AT+MQTTPUB=0,\"%s\",\"%s\",1,1",
                          NPS_TOPIC_STATUS, esc)) != ESP_AT_OK))
    {
        return false;
    }

    printk("[mqtt] online in %lu ms\n", (unsigned long)(os_uptime_ms() - t0));
    return true;
}

/* Publish every dirty channel; a failed publish leaves it dirty. */
static void flush_states(void)
{
    char topic[64];
    char msg[48];
    char esc[96];

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

        if ((cmd_codec_format_state_topic(topic, sizeof(topic), NPS_TOPIC_PREFIX, ch) < 0) ||
            (cmd_codec_format_state(msg, sizeof(msg), relay_get(ch),
                                    relay_src_name(s_src[ch])) < 0) ||
            (at_escape(esc, sizeof(esc), msg) < 0))
        {
            continue;
        }

        if (track(esp_at_cmd(PUB_TIMEOUT_MS, "AT+MQTTPUB=0,\"%s\",\"%s\",1,1",
                             topic, esc)) != ESP_AT_OK)
        {
            __disable_irq();
            s_dirty |= bit;
            __enable_irq();
            return;
        }
    }
}

/* ── Task ───────────────────────────────────────────────────────────────── */

static void mqtt_link_task(void *param)
{
    (void)param;

    link_state_t state       = ST_ESP_INIT;
    uint32_t     backoff_ms  = NPS_RECONNECT_MIN_MS;
    uint32_t     next_try_ms = 0;

    set_state(&state, ST_ESP_INIT);

    /* uart_mgmt brings UART_ESP up after the scheduler starts. */
    while (!esp_at_uart_ready())
    {
        idle_tick();
        os_thread_delay(TICK_MS);
    }

    for (;;)
    {
        uint32_t now = os_uptime_ms();
        g_nps_diag.wifi_up = s_wifi_up;
        g_nps_diag.mqtt_up = s_mqtt_up;
        g_nps_diag.rx_overruns = esp_at_rx_overruns();

        if (s_esp_rebooted || (s_timeouts >= NPS_ESP_MAX_TIMEOUTS))
        {
            printk("[esp] %s, resetting\n", s_esp_rebooted ? "rebooted unexpectedly"
                                                          : "stopped answering");
            set_state(&state, ST_ESP_INIT);
        }

        bool rejoined = (state == ST_WIFI) && s_wifi_up;   /* ESP auto-reconnected */
        if ((state != ST_ESP_INIT) && (state != ST_ONLINE) && !rejoined &&
            ((int32_t)(now - next_try_ms) < 0))
        {
            esp_at_poll(TICK_MS);
            continue;
        }

        switch (state)
        {
            case ST_ESP_INIT:
                if (step_esp_init())
                {
                    set_state(&state, ST_WIFI);
                    next_try_ms = now;
                }
                else
                {
                    /* Hold off before resetting the ESP again. */
                    for (uint32_t t = 0; t < 5000U; t += TICK_MS)
                    {
                        esp_at_poll(TICK_MS);
                    }
                }
                break;

            case ST_WIFI:
                if (s_wifi_up || step_wifi_join())
                {
                    set_state(&state, ST_MQTT);
                    next_try_ms = os_uptime_ms();
                    backoff_ms  = NPS_RECONNECT_MIN_MS;
                    break;
                }
                next_try_ms = os_uptime_ms() + backoff_ms;
                backoff_ms  = (backoff_ms * 2U > NPS_RECONNECT_MAX_MS)
                            ? NPS_RECONNECT_MAX_MS : backoff_ms * 2U;
                break;

            case ST_MQTT:
                if (!s_wifi_up)
                {
                    set_state(&state, ST_WIFI);
                    break;
                }
                if (step_mqtt_connect())
                {
                    __disable_irq();
                    s_dirty = (1UL << NPS_RELAY_COUNT) - 1UL;   /* re-announce all */
                    __enable_irq();
                    g_nps_diag.online_count++;
                    backoff_ms = NPS_RECONNECT_MIN_MS;
                    set_state(&state, ST_ONLINE);
                    break;
                }
                next_try_ms = os_uptime_ms() + backoff_ms;
                backoff_ms  = (backoff_ms * 2U > NPS_RECONNECT_MAX_MS)
                            ? NPS_RECONNECT_MAX_MS : backoff_ms * 2U;
                break;

            case ST_ONLINE:
                if (!s_wifi_up || !s_mqtt_up)
                {
                    printk("[mqtt] %s lost; relays held\n", s_wifi_up ? "broker" : "Wi-Fi");
                    s_mqtt_up = false;
                    set_state(&state, s_wifi_up ? ST_MQTT : ST_WIFI);
                    next_try_ms = os_uptime_ms() + backoff_ms;
                    break;
                }
                if (s_dirty != 0U)
                {
                    flush_states();
                }
                esp_at_poll(TICK_MS);
                break;
        }
    }
}

int32_t mqtt_link_start(void)
{
    /* Escaping failures mean a credential longer than the buffers above,
     * which are sized from the literals themselves — cannot happen. */
    (void)at_escape(s_ssid_esc,  sizeof(s_ssid_esc),  NPS_WIFI_SSID);
    (void)at_escape(s_wpass_esc, sizeof(s_wpass_esc), NPS_WIFI_PASS);
    (void)at_escape(s_user_esc,  sizeof(s_user_esc),  NPS_MQTT_USER);
    (void)at_escape(s_mpass_esc, sizeof(s_mpass_esc), NPS_MQTT_PASS);
    (void)at_escape(s_lwt_esc,   sizeof(s_lwt_esc),   NPS_STATUS_OFFLINE_MSG);

    for (uint8_t ch = 0; ch <= NPS_RELAY_COUNT; ch++)
    {
        s_src[ch] = RELAY_SRC_BOOT;
    }

    esp_at_set_urc_handler(on_urc);
    esp_at_set_idle_hook(idle_tick);
    relay_set_observer(on_relay_changed);

    int32_t id = os_thread_create(mqtt_link_task, "mqtt", NPS_MQTT_TASK_STACK,
                                  NPS_MQTT_TASK_PRIO, NULL);
    return (id < 0) ? id : OS_ERR_NONE;
}
