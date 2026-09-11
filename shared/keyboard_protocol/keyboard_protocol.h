/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_KEYBOARD_PROTOCOL_H
#define EF800_KEYBOARD_PROTOCOL_H

#include <stdint.h>

/* UART framing used by the EF800 keyboard link. */
#define KB_UART_BAUDRATE 115200U
#define KB_UART_DATA_BITS 8U
#define KB_UART_STOP_BITS 1U

/* Protocol bytes.  The short names intentionally match mbed-uart-keyboard. */
#define CMD_ACK 0x7FU
#define CMD_RESEND 0xFEU
#define CMD_RETURN 0xA0U
#define CMD_INITIALIZED 0xAAU

#define CMD_KEYBOARD_ACTIVATE 0xF4U
#define CMD_KEYBOARD_DEACTIVATE 0xF5U
#define CMD_KEYBOARD_RESET 0xFFU
#define CMD_KEYBOARD_BOOTLOADER 0xE2U
#define CMD_KEYBOARD_TYPEMATIC 0xF3U
#define CMD_KEYBOARD_SET_DEBUG 0xF6U
#define CMD_KEYBOARD_ECHO 0xEEU
#define CMD_KEYBOARD_DEBUG 0xEFU
#define CMD_KEYBOARD_ID 0xF2U

/*
 * Fixed-length virtual-key requests accepted while debug mode is enabled:
 * CMD_KEYBOARD_DEBUG, action, name length, name bytes. The request has no
 * response; a valid request produces only the normal make/break scancode.
 */
#define KB_DEBUG_ACTION_PRESS 0x01U
#define KB_DEBUG_ACTION_RELEASE 0x02U
#define KB_DEBUG_KEY_NAME_MAX 16U

/* Host-side flags used for command completion. */
#define ECHO_FLAG (1UL << 0)
#define KEYBOARD_ACTIVE (1UL << 1)
#define KEYBOARD_INACTIVE (1UL << 2)
#define KEYBOARD_RESETTED (1UL << 3)
#define KEYBOARD_BOOTLOADER_STARTED (1UL << 4)
#define KEYBOARD_TYPEMATIC_SET (1UL << 5)
#define KEYBOARD_DEBUG_SET (1UL << 6)
#define KEYBOARD_ID (1UL << 7)

/* Modifier bit positions are part of the ASCII map ABI. */
#define MODIFIER_SHIFT 0
#define MODIFIER_ALT 1
#define MODIFIER_CTRL 2
#define KB_MODIFIER_NONE 0x00U
#define KB_MODIFIER_SHIFT (1U << MODIFIER_SHIFT)
#define KB_MODIFIER_ALT (1U << MODIFIER_ALT)
#define KB_MODIFIER_CTRL (1U << MODIFIER_CTRL)
#define KB_MODIFIER_SHIFTALT (KB_MODIFIER_SHIFT | KB_MODIFIER_ALT)
#define KB_MODIFIER_ALTCTRL (KB_MODIFIER_ALT | KB_MODIFIER_CTRL)
#define KB_MODIFIER_CTRLSHIFT (KB_MODIFIER_CTRL | KB_MODIFIER_SHIFT)
#define KB_MODIFIER_CTRLALTSHIFT \
	(KB_MODIFIER_CTRL | KB_MODIFIER_ALT | KB_MODIFIER_SHIFT)

/* Event values are bit flags in the original host API. */
#define KB_EVENT_ON (1UL << 0)
#define KB_EVENT_OFF (1UL << 1)
#define KB_EVENT_INSERT (1UL << 2)
#define KB_EVENT_PAST (1UL << 3)
#define KB_EVENT_ARROWUP (1UL << 10)
#define KB_EVENT_ARROWDOWN (1UL << 11)
#define KB_EVENT_ARROWLEFT (1UL << 12)
#define KB_EVENT_ARROWRIGHT (1UL << 13)
#define KB_EVENT_FUNC1 (1UL << 14)
#define KB_EVENT_FUNC2 (1UL << 15)
#define KB_EVENT_FUNC3 (1UL << 16)
#define KB_EVENT_FUNC4 (1UL << 17)
#define KB_EVENT_FUNC5 (1UL << 18)
#define KB_EVENT_FUNC6 (1UL << 19)
#define KB_EVENT_KEY (1UL << 30)
#define KB_EVENT_ALL_WO_OFF                                                \
	(KB_EVENT_ON | KB_EVENT_INSERT | KB_EVENT_PAST | KB_EVENT_ARROWUP |     \
	 KB_EVENT_ARROWDOWN | KB_EVENT_ARROWLEFT | KB_EVENT_ARROWRIGHT |        \
	 KB_EVENT_FUNC1 | KB_EVENT_FUNC2 | KB_EVENT_FUNC3 | KB_EVENT_FUNC4 |     \
	 KB_EVENT_FUNC5 | KB_EVENT_FUNC6 | KB_EVENT_KEY)
#define KB_EVENT_ALL                                                       \
	(KB_EVENT_ON | KB_EVENT_OFF | KB_EVENT_INSERT | KB_EVENT_PAST |       \
	 KB_EVENT_ARROWUP | KB_EVENT_ARROWDOWN | KB_EVENT_ARROWLEFT |          \
	 KB_EVENT_ARROWRIGHT | KB_EVENT_FUNC1 | KB_EVENT_FUNC2 |               \
	 KB_EVENT_FUNC3 | KB_EVENT_FUNC4 | KB_EVENT_FUNC5 | KB_EVENT_FUNC6 |   \
	 KB_EVENT_KEY)

/* Wire prefixes used by the PS/2-style scancode stream. */
#define KB_SCANCODE_PREFIX_NONE 0x00U
#define KB_SCANCODE_PREFIX_E0 0xE0U
#define KB_SCANCODE_PREFIX_E1 0xE1U
#define KB_SCANCODE_BREAK 0xF0U

#define KB_MATRIX_ROWS 8U
#define KB_MATRIX_COLS 7U
#define KB_SPECIAL_KEYS 5U
#define KB_SPECIAL_KEY_OFFSET (KB_MATRIX_ROWS << 4)

#define KB_MATRIX_RAW(row, col) ((((uint16_t)(row)) << 4) | (uint16_t)(col))
#define KB_SPECIAL_RAW(key) ((uint16_t)(KB_SPECIAL_KEY_OFFSET + (key)))

#endif /* EF800_KEYBOARD_PROTOCOL_H */
