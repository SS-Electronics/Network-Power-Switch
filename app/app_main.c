/*
 * app_main.c — Network-Power-Switch application entry point
 *
 * Board: STM32F401CDU6 + ESP8266 (ESP-AT firmware on USART1)
 *        + 4-channel active-LOW relay module
 *
 * A phone app presses a button → the server publishes
 * nps/<id>/relay/<n>/set {"on":true} → the ESP8266 delivers it over UART →
 * this board switches relay n and publishes the confirmed state back
 * (retained). Contract: docs/mqtt_api.md.
 *
 * Tasks
 * ─────
 *   mqtt    (prio 5)  owns the ESP8266: Wi-Fi + MQTT/TLS via AT commands
 *   button  (prio 3)  KEY toggles NPS_BUTTON_CHANNEL locally
 *   WDOG    (prio 4)  IWDG fed only while mqtt + button slots are kicked
 *
 * Safety
 * ──────
 *   - Relays are driven OFF before anything else runs; every reset (power
 *     loss, watchdog, fault) therefore lands with all loads OFF.
 *   - Wi-Fi/broker loss holds the current relay state.
 *   - An ESP8266 that stops answering is reset by the MCU; an MCU task that
 *     hangs stops its watchdog slot → IWDG reset.
 */

#include <os/kernel.h>
#include <os/kernel_syscall.h>
#include <safety/wdog.h>

#include "conf_app.h"
#include "relay.h"
#include "button.h"
#include "esp_at.h"
#include "mqtt_link.h"

int app_main(void)
{
    int32_t st;

    /* First: every relay OFF and owned by relay.c. */
    relay_init();

    wdog_sw_init((1UL << NPS_WDOG_SLOT_MQTT) | (1UL << NPS_WDOG_SLOT_BUTTON));

    st = esp_at_init();
    if (st != OS_ERR_NONE)
    {
        return st;
    }

    st = button_start();
    if (st != OS_ERR_NONE)
    {
        return st;
    }

    return mqtt_link_start();
}
