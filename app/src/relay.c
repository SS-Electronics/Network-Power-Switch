/**
 * @file        relay.c
 * @brief       4-channel active-LOW relay output driver
 *
 * @info        The pins are driven here with the HAL directly rather than
 *              through gpio_mgmt: the OS GPIO path switches a pin to output
 *              with ODR=0 and ignores active_state, which on an active-LOW
 *              module means every relay energises during boot. Writing the
 *              OFF level to ODR before the mode change avoids that.
 */

#include <device.h>
#include <os/kernel.h>
#include <os/kernel_syscall.h>

#include "conf_app.h"
#include "relay.h"

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t      pin;
} relay_pin_t;

static const relay_pin_t s_pins[NPS_RELAY_COUNT] = NPS_RELAY_PINS;

static volatile bool    s_state[NPS_RELAY_COUNT];
static relay_observer_t s_observer;
static os_mutex_t       s_lock;

static void relay_clk_enable(GPIO_TypeDef *port)
{
    if (port == GPIOA)
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    }
    else if (port == GPIOB)
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    }
    else if (port == GPIOC)
    {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    }
}

/* Active-LOW: ON sinks the input, OFF releases it to the module pull-up. */
static void relay_drive(uint8_t idx, bool on)
{
    HAL_GPIO_WritePin(s_pins[idx].port, s_pins[idx].pin,
                      on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void relay_init(void)
{
    GPIO_InitTypeDef cfg = {0};

    for (uint8_t i = 0; i < NPS_RELAY_COUNT; i++)
    {
        relay_clk_enable(s_pins[i].port);

        /* Latch OFF in ODR first so the output stage comes up released. */
        relay_drive(i, false);

        cfg.Pin   = s_pins[i].pin;
        cfg.Mode  = GPIO_MODE_OUTPUT_OD;
        cfg.Pull  = GPIO_NOPULL;
        cfg.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(s_pins[i].port, &cfg);

        s_state[i] = false;
    }

    s_lock = os_mutex_create();
}

void relay_set_observer(relay_observer_t cb)
{
    s_observer = cb;
}

bool relay_set(uint8_t channel, bool on, relay_src_t src)
{
    if ((channel == 0U) || (channel > NPS_RELAY_COUNT))
    {
        return false;
    }

    uint8_t idx = (uint8_t)(channel - 1U);

    /* Serialises button vs MQTT so the observer sees changes in pin order. */
    (void)os_mutex_lock(s_lock, OS_WAIT_FOREVER);
    relay_drive(idx, on);
    s_state[idx] = on;
    (void)os_mutex_unlock(s_lock);

    printk("[relay] ch%u %s (%s)\n", (unsigned)channel, on ? "ON" : "OFF",
           relay_src_name(src));

    if (s_observer != NULL)
    {
        s_observer(channel, on, src);
    }

    return true;
}

bool relay_toggle(uint8_t channel, relay_src_t src)
{
    if ((channel == 0U) || (channel > NPS_RELAY_COUNT))
    {
        return false;
    }

    return relay_set(channel, !s_state[channel - 1U], src);
}

bool relay_get(uint8_t channel)
{
    if ((channel == 0U) || (channel > NPS_RELAY_COUNT))
    {
        return false;
    }

    return s_state[channel - 1U];
}

bool relay_any_on(void)
{
    for (uint8_t i = 0; i < NPS_RELAY_COUNT; i++)
    {
        if (s_state[i])
        {
            return true;
        }
    }

    return false;
}

const char *relay_src_name(relay_src_t src)
{
    switch (src)
    {
        case RELAY_SRC_APP:    return "app";
        case RELAY_SRC_BUTTON: return "button";
        case RELAY_SRC_BOOT:
        default:               return "boot";
    }
}
