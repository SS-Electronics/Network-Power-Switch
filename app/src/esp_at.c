/**
 * @file        esp_at.c
 * @brief       ESP8266 (ESP-AT firmware) UART transport
 *
 * @info        RX: USART RXNE ISR → OS irq chain → rx_isr() → FreeRTOS stream
 *              buffer → owner task frames lines (at_parse.c).
 *              TX: drv_serial_transmit() (blocking HAL transmit). The OS gives
 *              each transmit a fixed 10 ms budget, ~115 bytes at 115200 baud,
 *              so commands go out in 64-byte chunks.
 *
 *              The ESP reset line is driven here, not via the board XML: the
 *              OS gpio path would hold it LOW (ESP in reset) from boot.
 *              Open-drain + pull-up: released = running.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <device.h>
#include <os/kernel.h>
#include <os/kernel_syscall.h>
#include <drivers/drv_uart.h>
#include <irq/irq_notify.h>
#include <board/board_device_ids.h>

#include "FreeRTOS.h"
#include "stream_buffer.h"

#include "conf_app.h"
#include "esp_at.h"

#define RX_STREAM_SIZE      1024U
#define TX_CHUNK            64U
#define POLL_SLICE_MS       100U

static StreamBufferHandle_t s_rx;
static volatile uint32_t    s_rx_overruns;
static at_lineasm_t         s_asm;
static char                 s_tx[400];
static esp_at_urc_cb_t      s_urc;
static esp_at_idle_cb_t     s_idle;

/* ── RX ─────────────────────────────────────────────────────────────────── */

static void rx_isr(irq_id_t id, void *data, void *arg, BaseType_t *pxHPT)
{
    (void)id;
    (void)arg;

    if (xStreamBufferSendFromISR(s_rx, data, 1, pxHPT) != 1U)
    {
        s_rx_overruns++;
    }
}

/* Bytes pulled from the stream but not yet framed: a chunk can hold the
 * end of one line and the start of the next. */
static uint8_t s_chunk[32];
static size_t  s_chunk_n;
static size_t  s_chunk_i;

static void rx_discard(void)
{
    xStreamBufferReset(s_rx);
    s_chunk_n = 0;
    s_chunk_i = 0;
    at_lineasm_reset(&s_asm);
}

/* Next complete line, or NULL after timeout_ms. Calls the idle hook. */
static const char *next_line(uint32_t timeout_ms, size_t *len)
{
    uint32_t start = os_uptime_ms();

    for (;;)
    {
        while (s_chunk_i < s_chunk_n)
        {
            if (at_lineasm_feed(&s_asm, s_chunk[s_chunk_i++]))
            {
                *len = s_asm.len;
                return s_asm.buf;
            }
        }

        uint32_t elapsed = os_uptime_ms() - start;
        if (elapsed >= timeout_ms)
        {
            return NULL;
        }

        uint32_t slice = timeout_ms - elapsed;
        if (slice > POLL_SLICE_MS)
        {
            slice = POLL_SLICE_MS;
        }

        s_chunk_n = xStreamBufferReceive(s_rx, s_chunk, sizeof(s_chunk), pdMS_TO_TICKS(slice));
        s_chunk_i = 0;

        if (s_idle != NULL)
        {
            s_idle();
        }
    }
}

/* ── TX ─────────────────────────────────────────────────────────────────── */

static esp_at_result_t tx(const char *s, size_t n)
{
    while (n > 0U)
    {
        size_t c = (n > TX_CHUNK) ? TX_CHUNK : n;
        if (drv_serial_transmit(UART_ESP, (const uint8_t *)s, (uint16_t)c) != OS_ERR_NONE)
        {
            return ESP_AT_IO;
        }
        s += c;
        n -= c;
    }
    return ESP_AT_OK;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

int32_t esp_at_init(void)
{
    GPIO_InitTypeDef cfg = {0};

    /* Reset line released (high via pull-up) before it becomes an output. */
    NPS_ESP_RST_CLK_ENABLE();
    HAL_GPIO_WritePin(NPS_ESP_RST_PORT, NPS_ESP_RST_PIN, GPIO_PIN_SET);
    cfg.Pin   = NPS_ESP_RST_PIN;
    cfg.Mode  = GPIO_MODE_OUTPUT_OD;
    cfg.Pull  = GPIO_PULLUP;
    cfg.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(NPS_ESP_RST_PORT, &cfg);

    at_lineasm_reset(&s_asm);

    s_rx = xStreamBufferCreate(RX_STREAM_SIZE, 1);
    if (s_rx == NULL)
    {
        return OS_ERR_MEM_OF;
    }

    return (irq_register(IRQ_ID_UART_RX(UART_ESP), rx_isr, NULL) == 0)
           ? OS_ERR_NONE : OS_ERR_OP;
}

void esp_at_set_urc_handler(esp_at_urc_cb_t cb) { s_urc = cb; }
void esp_at_set_idle_hook(esp_at_idle_cb_t cb)  { s_idle = cb; }
uint32_t esp_at_rx_overruns(void)               { return s_rx_overruns; }

bool esp_at_uart_ready(void)
{
    drv_uart_handle_t *h = drv_uart_get_handle(UART_ESP);
    return (h != NULL) && h->initialized;
}

bool esp_at_wait_for(at_line_t kind, uint32_t timeout_ms)
{
    uint32_t start = os_uptime_ms();

    for (;;)
    {
        uint32_t elapsed = os_uptime_ms() - start;
        size_t len;
        const char *line;

        if (elapsed >= timeout_ms)
        {
            return false;
        }

        line = next_line(timeout_ms - elapsed, &len);
        if (line == NULL)
        {
            return false;
        }

        at_line_t k = at_classify(line, len);
        if (k == kind)
        {
            return true;
        }
        if (s_urc != NULL)
        {
            s_urc(k, line, len);
        }
    }
}

bool esp_at_hw_reset(uint32_t timeout_ms)
{
    HAL_GPIO_WritePin(NPS_ESP_RST_PORT, NPS_ESP_RST_PIN, GPIO_PIN_RESET);
    os_thread_delay(100);
    rx_discard();
    HAL_GPIO_WritePin(NPS_ESP_RST_PORT, NPS_ESP_RST_PIN, GPIO_PIN_SET);

    return esp_at_wait_for(AT_LINE_READY, timeout_ms);
}

esp_at_result_t esp_at_cmd(uint32_t timeout_ms, const char *fmt, ...)
{
    va_list ap;
    int n;
    uint32_t start;

    va_start(ap, fmt);
    n = vsnprintf(s_tx, sizeof(s_tx) - 2U, fmt, ap);
    va_end(ap);

    if ((n < 0) || ((size_t)n >= sizeof(s_tx) - 2U))
    {
        return ESP_AT_IO;
    }
    s_tx[n++] = '\r';
    s_tx[n++] = '\n';

    if (tx(s_tx, (size_t)n) != ESP_AT_OK)
    {
        return ESP_AT_IO;
    }

    start = os_uptime_ms();
    for (;;)
    {
        uint32_t elapsed = os_uptime_ms() - start;
        size_t len;
        const char *line;

        if (elapsed >= timeout_ms)
        {
            return ESP_AT_TIMEOUT;
        }

        line = next_line(timeout_ms - elapsed, &len);
        if (line == NULL)
        {
            return ESP_AT_TIMEOUT;
        }

        at_line_t k = at_classify(line, len);
        if (k == AT_LINE_OK)
        {
            return ESP_AT_OK;
        }
        if ((k == AT_LINE_ERROR) || (k == AT_LINE_FAIL))
        {
            return ESP_AT_ERROR;
        }
        if (s_urc != NULL)
        {
            s_urc(k, line, len);
        }
    }
}

void esp_at_poll(uint32_t timeout_ms)
{
    uint32_t start = os_uptime_ms();

    for (;;)
    {
        uint32_t elapsed = os_uptime_ms() - start;
        size_t len;
        const char *line;

        if (elapsed >= timeout_ms)
        {
            return;
        }

        line = next_line(timeout_ms - elapsed, &len);
        if (line == NULL)
        {
            return;
        }
        if (s_urc != NULL)
        {
            s_urc(at_classify(line, len), line, len);
        }
    }
}
