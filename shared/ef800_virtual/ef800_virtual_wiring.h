/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_VIRTUAL_WIRING_H
#define EF800_VIRTUAL_WIRING_H

#include <stdbool.h>
#include <stdint.h>

uint16_t ef800_virtual_wiring_mcp_state(void);
bool ef800_virtual_wiring_keyboard_reset_released(void);
bool ef800_virtual_wiring_lcd_reset_released(void);
bool ef800_virtual_wiring_lcd_fs_active(void);
bool ef800_virtual_wiring_usb_selected(void);

#endif
