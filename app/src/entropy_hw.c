/**
 * @file        entropy_hw.c
 * @brief       mbedTLS hardware entropy source on the STM32H7 TRNG
 *
 * @info        MBEDTLS_ENTROPY_HARDWARE_ALT makes mbedtls_entropy_func() pull
 *              from mbedtls_hardware_poll(). The RNG kernel clock defaults to
 *              HSI48, which FreeRTOS-OS leaves off, so it is started here on
 *              first use. Runs in tcpip-thread context (TLS handshake).
 */

#include <string.h>

#include <device.h>

#include "mbedtls/entropy.h"

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen);

static RNG_HandleTypeDef s_rng;
static int s_ready;

static int entropy_hw_init(void)
{
    uint32_t start;

    __HAL_RCC_HSI48_ENABLE();
    start = HAL_GetTick();
    while (__HAL_RCC_GET_FLAG(RCC_FLAG_HSI48RDY) == 0U)
    {
        if ((HAL_GetTick() - start) > 10U)
        {
            return -1;
        }
    }

    __HAL_RCC_RNG_CONFIG(RCC_RNGCLKSOURCE_HSI48);
    __HAL_RCC_RNG_CLK_ENABLE();

    s_rng.Instance = RNG;
    s_rng.Init.ClockErrorDetection = RNG_CED_ENABLE;
    if (HAL_RNG_Init(&s_rng) != HAL_OK)
    {
        return -1;
    }

    s_ready = 1;
    return 0;
}

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
    (void)data;
    *olen = 0;

    if ((s_ready == 0) && (entropy_hw_init() != 0))
    {
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }

    while (*olen < len)
    {
        uint32_t word;
        size_t n = len - *olen;

        /* Seed/clock errors surface as HAL_ERROR; mbedTLS must see a failure
         * rather than be handed predictable bytes. */
        if (HAL_RNG_GenerateRandomNumber(&s_rng, &word) != HAL_OK)
        {
            return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
        }

        if (n > sizeof(word))
        {
            n = sizeof(word);
        }
        memcpy(output + *olen, &word, n);
        *olen += n;
    }

    return 0;
}
