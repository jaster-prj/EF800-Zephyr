/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_KEYBOARD_UPDATE_PROTOCOL_H_
#define EF800_KEYBOARD_UPDATE_PROTOCOL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EF800_KEYBOARD_UPDATER_I2C_ADDRESS 0x48U
#define EF800_KB_UPDATE_VERSION_COMMAND 0xF2U
#define EF800_KB_UPDATE_VERSION_ACK 0x06U
#define EF800_KB_UPDATE_VERSION_SIZE 5U

#define EF800_KB_UPDATE_CMD_GET 0x00U
#define EF800_KB_UPDATE_CMD_GET_ID 0x02U
#define EF800_KB_UPDATE_CMD_READ_MEMORY 0x11U
#define EF800_KB_UPDATE_CMD_WRITE_MEMORY 0x31U
#define EF800_KB_UPDATE_CMD_ERASE 0x44U
#define EF800_KB_UPDATE_CMD_BEGIN 0xF3U
#define EF800_KB_UPDATE_CMD_CHECK 0xF4U
#define EF800_KB_UPDATE_CMD_RUN 0xF5U

#define EF800_KB_PROTOCOL_ACK 0x79U
#define EF800_KB_PROTOCOL_NACK 0x1FU
#define EF800_KB_PROTOCOL_BUSY 0x76U
#define EF800_KB_UPDATE_FRAME_TIMEOUT_MS 1000U
#define EF800_KB_UPDATE_BLOCK_MAX 256U
#define EF800_KB_UPDATE_REQUEST_MAX 266U
#define EF800_KB_UPDATE_RESPONSE_MAX 260U
#define EF800_KB_UPDATE_PAGE_SIZE 2048U
#define EF800_KB_UPDATE_WRITE_ALIGNMENT 8U

#define EF800_KB_UPDATE_ERASE_PARTITION 0U
#define EF800_KB_UPDATE_ERASE_BLOCK 1U

enum ef800_kb_update_target {
	EF800_KB_UPDATE_TARGET_APPLICATION = 1U,
	EF800_KB_UPDATE_TARGET_BOOTLOADER = 2U,
	EF800_KB_UPDATE_TARGET_UPDATER = 3U,
};

enum ef800_kb_update_service {
	EF800_KB_UPDATE_SERVICE_KEYBOARD_APPLICATION = 1U,
	EF800_KB_UPDATE_SERVICE_UPDATER = 2U,
};

/* AN4221-style command/complement pair and XOR frame checksum helpers. */
bool ef800_kb_update_command_valid(uint8_t command, uint8_t complement);
uint8_t ef800_kb_update_checksum(const uint8_t *data, size_t length);
bool ef800_kb_update_frame_valid(const uint8_t *data, size_t length);
size_t ef800_kb_update_frame_encode(uint8_t command, const uint8_t *payload,
				     size_t payload_length, uint8_t *frame,
				     size_t capacity);
bool ef800_kb_update_response_valid(const uint8_t *response, size_t length);
bool ef800_kb_update_frame_timing_valid(uint32_t previous_ms, uint32_t now_ms);

/* Address and page checks are derived from the closed target enum. */
bool ef800_kb_update_target_range(enum ef800_kb_update_target target,
				  uint32_t *base, uint32_t *size);
bool ef800_kb_update_service_target_valid(
	enum ef800_kb_update_service service,
	enum ef800_kb_update_target target);
bool ef800_kb_update_range_valid(enum ef800_kb_update_target target,
				 uint32_t address, size_t length);
bool ef800_kb_update_page_valid(enum ef800_kb_update_target target,
				uint16_t page);
bool ef800_kb_update_write_valid(enum ef800_kb_update_target target,
				 uint32_t address, size_t length);
size_t ef800_kb_update_version_encode(uint8_t command, uint8_t ack,
				      uint8_t major, uint8_t minor, uint8_t patch,
				      uint8_t *response, size_t capacity);

#endif /* EF800_KEYBOARD_UPDATE_PROTOCOL_H_ */
