/**
 * @file        button.h
 * @brief       B1 user button: local toggle of one relay channel
 */

#ifndef APP_BUTTON_H_
#define APP_BUTTON_H_

#include <stdint.h>

/**
 * @brief Create the button task (EXTI13 → task notify → debounce → toggle).
 * @return OS_ERR_NONE or a negative OS_ERR_* code.
 */
int32_t button_start(void);

#endif /* APP_BUTTON_H_ */
