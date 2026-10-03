/**
 * @file        button.c
 * @brief       KEY user button (PA0): local toggle of one relay channel
 *
 * @info        KEY (PA0, to GND, pull-up) falling edge → EXTI0 →
 *              IRQ_ID_EXTI(0) → this task. A press only counts if the pin
 *              still reads NPS_BUTTON_PRESSED_LEVEL after the debounce delay;
 *              edges that arrive during it are discarded.
 *              The relay observer publishes the change, so the app sees a
 *              local toggle the same way as its own command.
 */

#include <os/kernel.h>
#include <os/kernel_syscall.h>
#include <drivers/drv_gpio.h>
#include <irq/irq_desc.h>
#include <board/board_device_ids.h>
#include <safety/wdog.h>

#include "conf_app.h"
#include "relay.h"
#include "button.h"

#define BTN_EXTI_LINE       0
#define BTN_WAKE_MS         500U    /* bounded wait so the wdog slot is kicked */

static irqreturn_t button_irq(irq_id_t irq, void *data, void *dev_id, BaseType_t *pxHPT)
{
    (void)irq;
    (void)data;

    vTaskNotifyGiveFromISR((TaskHandle_t)dev_id, pxHPT);
    return IRQ_HANDLED;
}

static void button_task(void *param)
{
    (void)param;

    (void)request_irq(IRQ_ID_EXTI(BTN_EXTI_LINE), button_irq, "btn_user",
                      xTaskGetCurrentTaskHandle());

    for (;;)
    {
        wdog_task_kick(NPS_WDOG_SLOT_BUTTON);

        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(BTN_WAKE_MS)) == 0U)
        {
            continue;
        }

        os_thread_delay(NPS_BUTTON_DEBOUNCE_MS);
        (void)ulTaskNotifyTake(pdTRUE, 0);      /* drop bounce edges */

        if (drv_gpio_read_pin(BTN_USER) == NPS_BUTTON_PRESSED_LEVEL)
        {
            (void)relay_toggle(NPS_BUTTON_CHANNEL, RELAY_SRC_BUTTON);
        }
    }
}

int32_t button_start(void)
{
    int32_t id = os_thread_create(button_task, "button", NPS_BUTTON_TASK_STACK,
                                  NPS_BUTTON_TASK_PRIO, NULL);
    return (id < 0) ? id : OS_ERR_NONE;
}
