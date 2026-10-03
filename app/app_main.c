/*
 * app_main.c — Network-Power-Switch application entry point
 *
 * Board: NUCLEO-H723ZG (STM32H723ZGTx) + 4-channel active-LOW relay module
 *
 * A phone app presses a button → the server publishes
 * nps/<id>/relay/<n>/set {"on":true} → this board switches relay n and
 * publishes the confirmed state back (retained). Contract: docs/mqtt_api.md.
 *
 * Tasks
 * ─────
 *   mqtt      (prio 5)  MQTT-over-TLS session, commands in / state out
 *   button    (prio 3)  B1 toggles NPS_BUTTON_CHANNEL locally
 *   net_guard (prio 2)  only if Ethernet failed at boot: reboot-retry
 *   tcpip/net_rx         lwIP (FreeRTOS-OS net service)
 *   WDOG      (prio 4)  IWDG fed only while mqtt + button slots are kicked
 *
 * Safety
 * ──────
 *   - Relays are driven OFF before anything else runs; every reset (power
 *     loss, watchdog, fault) therefore lands with all loads OFF.
 *   - Link/broker loss holds the current relay state.
 *   - A wedged tcpip thread stops the mqtt slot kick → IWDG reset.
 */

#include <os/kernel.h>
#include <os/kernel_syscall.h>
#include <board/board_device_ids.h>
#include <safety/wdog.h>
#include <net/net_service.h>
#include <device.h>

#include "conf_app.h"
#include "relay.h"
#include "button.h"
#include "mqtt_link.h"

/*
 * FreeRTOS-OS cannot bring Ethernet up after boot (the PHY must link inside
 * net_service_start()). Without a network there is nothing for the MQTT task
 * to do, so retry the whole boot — but never while a load is ON.
 */
static void net_guard_task(void *param)
{
    (void)param;
    uint32_t start = os_uptime_ms();

    os_thread_delay(3000);      /* printk is gated off for the first ~2 s */
    printk("[net] no Ethernet link at boot; reboot-retry in %lu s (only while all relays are OFF)\n",
           (unsigned long)(NPS_NET_RETRY_REBOOT_MS / 1000U));

    for (;;)
    {
        os_thread_delay(1000);
        if (((os_uptime_ms() - start) >= NPS_NET_RETRY_REBOOT_MS) && !relay_any_on())
        {
            printk("[net] rebooting to retry Ethernet bring-up\n");
            os_thread_delay(50);
            NVIC_SystemReset();
        }
    }
}

int app_main(void)
{
    int32_t st;

    /* First: every relay OFF and owned by relay.c. */
    relay_init();

    wdog_sw_init((1UL << NPS_WDOG_SLOT_MQTT) | (1UL << NPS_WDOG_SLOT_BUTTON));

    st = button_start();
    if (st != OS_ERR_NONE)
    {
        return st;
    }

    if (net_service_start() != OS_ERR_NONE)
    {
        /* The MQTT task still runs (and keeps its watchdog slot alive); it
         * just never sees a link. */
        (void)os_thread_create(net_guard_task, "net_guard", NPS_NET_GUARD_STACK,
                               NPS_NET_GUARD_PRIO, NULL);
    }

    return mqtt_link_start();
}
