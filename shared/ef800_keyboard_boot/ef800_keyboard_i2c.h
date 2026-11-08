/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_KEYBOARD_I2C_H_
#define EF800_KEYBOARD_I2C_H_

#include <stdint.h>

/* Keyboard-application I2C target; updater has its own address (0x48). */
#define EF800_KEYBOARD_I2C_ADDRESS 0x2AU

#define EF800_KEYBOARD_I2C_CMD_STATUS 0xF0U
#define EF800_KEYBOARD_I2C_CMD_ENTER_UPDATER 0xF1U
/* Transitional name retained for host code until the updater transport lands. */
#define EF800_KEYBOARD_I2C_CMD_ENTER_ROM EF800_KEYBOARD_I2C_CMD_ENTER_UPDATER
#define EF800_KEYBOARD_I2C_CMD_VERSION 0xF2U
#define EF800_KEYBOARD_I2C_ACK 0x06U

#define EF800_KEYBOARD_I2C_MODE_APPLICATION 0x01U
#define EF800_KEYBOARD_I2C_STATUS_SIZE 6U
#define EF800_KEYBOARD_I2C_VERSION_SIZE 5U

#endif /* EF800_KEYBOARD_I2C_H_ */
