/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Translate virtual host key messages into keyboard input state. */
#ifndef EF800_VIRTUAL_KEYBOARD_CONTROLLER_H
#define EF800_VIRTUAL_KEYBOARD_CONTROLLER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/** @brief Send encoded virtual keyboard bytes to the attached controller.
 * @param data Protocol bytes.
 * @param length Byte count.
 * @return 0 on send; negative errno if controller is unavailable or write fails.
 */
int ef800_virtual_keyboard_controller_send(const uint8_t *data, size_t length);

/** @brief Update whether the virtual keyboard reset line is released.
 * @param released True when the device is out of reset.
 * @return None.
 */
void ef800_virtual_keyboard_controller_reset_released(bool released);

#endif
