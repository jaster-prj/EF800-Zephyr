/* SPDX-License-Identifier: Apache-2.0 */

#ifndef EF800_KEYBOARD_BOOT_HANDOFF_H
#define EF800_KEYBOARD_BOOT_HANDOFF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool ef800_keyboard_boot_handoff(uintptr_t address, size_t size);

#endif /* EF800_KEYBOARD_BOOT_HANDOFF_H */
