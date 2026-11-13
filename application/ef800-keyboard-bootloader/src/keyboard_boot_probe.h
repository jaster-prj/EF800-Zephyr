/* SPDX-License-Identifier: Apache-2.0 */

#ifndef EF800_KEYBOARD_BOOT_PROBE_H
#define EF800_KEYBOARD_BOOT_PROBE_H

#include <stdbool.h>

int ef800_keyboard_boot_probe_gpio(bool *requested);
int ef800_keyboard_boot_probe_window(bool *received);
int ef800_keyboard_boot_probe_unregister(void);

#endif /* EF800_KEYBOARD_BOOT_PROBE_H */
