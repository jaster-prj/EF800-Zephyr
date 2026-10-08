/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_VIRTUAL_KEYBOARD_CONTROLLER_H
#define EF800_VIRTUAL_KEYBOARD_CONTROLLER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

int ef800_virtual_keyboard_controller_send(const uint8_t *data, size_t length);
void ef800_virtual_keyboard_controller_reset_released(bool released);

#endif
