/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Define keyboard updater commands, frame validation, and target bounds.
 */
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

/** @brief Validate a command and its bitwise-complement byte.
 * @param command Command byte.
 * @param complement Following complement byte.
 * @return True when the bytes form a valid pair.
 */
bool ef800_kb_update_command_valid(uint8_t command, uint8_t complement);

/** @brief Calculate the updater frame XOR checksum.
 * @param data Frame bytes covered by the checksum.
 * @param length Number of bytes.
 * @return XOR checksum byte.
 */
uint8_t ef800_kb_update_checksum(const uint8_t *data, size_t length);

/** @brief Validate frame boundaries, checksum, and command encoding.
 * @param data Complete frame bytes.
 * @param length Frame size.
 * @return True when the frame is structurally valid.
 */
bool ef800_kb_update_frame_valid(const uint8_t *data, size_t length);

/** @brief Encode a command, payload, and checksum into a bounded frame.
 * @param command Command byte.
 * @param payload Optional payload bytes.
 * @param payload_length Payload size.
 * @param frame Destination buffer.
 * @param capacity Destination capacity.
 * @return Encoded size, or zero when arguments/capacity are invalid.
 */
size_t ef800_kb_update_frame_encode(uint8_t command, const uint8_t *payload,
				     size_t payload_length, uint8_t *frame,
				     size_t capacity);

/** @brief Validate a response frame and its checksum/status bytes.
 * @param response Complete response bytes.
 * @param length Response size.
 * @return True when response encoding is valid.
 */
bool ef800_kb_update_response_valid(const uint8_t *response, size_t length);

/** @brief Check whether inter-frame timing satisfies the protocol deadline.
 * @param previous_ms Time of previous frame/byte.
 * @param now_ms Current time in wrapping millisecond ticks.
 * @return True when the elapsed interval is within the allowed bound.
 */
bool ef800_kb_update_frame_timing_valid(uint32_t previous_ms, uint32_t now_ms);

/* Address and page checks are derived from the closed target enum. */

/** @brief Resolve the authorized flash base and size for a closed target ID.
 * @param target Update target.
 * @param base Receives target base address.
 * @param size Receives target capacity.
 * @return True when target is recognized and outputs are written.
 */
bool ef800_kb_update_target_range(enum ef800_kb_update_target target,
				  uint32_t *base, uint32_t *size);

/** @brief Check whether a service is authorized to update a target.
 * @param service Requesting keyboard application or updater service.
 * @param target Requested image target.
 * @return True when this service-target pairing is permitted.
 */
bool ef800_kb_update_service_target_valid(
	enum ef800_kb_update_service service,
	enum ef800_kb_update_target target);

/** @brief Check that an address range lies entirely inside a target partition.
 * @param target Update target.
 * @param address Start address.
 * @param length Range length in bytes.
 * @return True when the nonempty range is fully authorized.
 */
bool ef800_kb_update_range_valid(enum ef800_kb_update_target target,
				 uint32_t address, size_t length);

/** @brief Check whether an erase page index is valid for a target.
 * @param target Update target.
 * @param page Relative erase-page index.
 * @return True when the page lies within the target partition.
 */
bool ef800_kb_update_page_valid(enum ef800_kb_update_target target,
				uint16_t page);

/** @brief Check target bounds and write alignment for a flash write.
 * @param target Update target.
 * @param address Absolute flash address.
 * @param length Write length.
 * @return True when the write is aligned and permitted.
 */
bool ef800_kb_update_write_valid(enum ef800_kb_update_target target,
				 uint32_t address, size_t length);

/** @brief Encode a fixed-size updater version response frame.
 * @param command Version command identifier.
 * @param ack Protocol acknowledgment byte.
 * @param major Major version number.
 * @param minor Minor version number.
 * @param patch Patch version number.
 * @param response Destination buffer.
 * @param capacity Destination capacity.
 * @return Encoded response length, or zero if capacity is insufficient.
 */
size_t ef800_kb_update_version_encode(uint8_t command, uint8_t ack,
				      uint8_t major, uint8_t minor, uint8_t patch,
				      uint8_t *response, size_t capacity);

#endif /* EF800_KEYBOARD_UPDATE_PROTOCOL_H_ */
