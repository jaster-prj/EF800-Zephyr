/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Transfer execution to a validated keyboard firmware image.
 */

#ifndef EF800_KEYBOARD_BOOT_HANDOFF_H
#define EF800_KEYBOARD_BOOT_HANDOFF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Set the processor state and branch to an image reset vector.
 * @param address Image base address.
 * @param size Image extent used to validate its vector table.
 * @return True if the handoff was attempted; false if image bounds are invalid.
 */
bool ef800_keyboard_boot_handoff(uintptr_t address, size_t size);

#endif /* EF800_KEYBOARD_BOOT_HANDOFF_H */
