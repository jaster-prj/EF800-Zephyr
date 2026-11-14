/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Encode/decode framed virtual-device messages and log records. */
#ifndef VIRTUAL_EF800_PROTOCOL_H
#define VIRTUAL_EF800_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define VIRTUAL_EF800_PROTOCOL_MAGIC 0x56454638U /* "VEF8" */
#define VIRTUAL_EF800_PROTOCOL_VERSION 1U
#define VIRTUAL_EF800_PROTOCOL_HEADER_SIZE 12U
#define VIRTUAL_EF800_PROTOCOL_MAX_PAYLOAD 2400U
#define VIRTUAL_EF800_COMPOSITION_STATE_MAX 2048U
#define VIRTUAL_EF800_COMPOSITION_ID_MAX 16U
#define VIRTUAL_EF800_LOG_MAX_TEXT 120U
#define VIRTUAL_EF800_LOG_HISTORY_COUNT 64U
#define VIRTUAL_EF800_LOG_HISTORY_BYTES 8192U
#define VIRTUAL_EF800_LOG_QUEUE_COUNT 32U
#define VIRTUAL_EF800_MTP_MAX_FILE_SIZE (32U * 1024U)
#define VIRTUAL_EF800_MTP_MAX_PATH 128U
#define VIRTUAL_EF800_MTP_MAX_NAME 96U
#define VIRTUAL_EF800_MTP_MAX_OBJECTS 12U
#define VIRTUAL_EF800_MTP_CHUNK_SIZE 2000U

enum virtual_ef800_log_severity {
	VIRTUAL_EF800_LOG_DEBUG = 0,
	VIRTUAL_EF800_LOG_INFO = 1,
	VIRTUAL_EF800_LOG_WARNING = 2,
	VIRTUAL_EF800_LOG_ERROR = 3,
	VIRTUAL_EF800_LOG_PANIC = 4,
};

enum virtual_ef800_log_flags {
	VIRTUAL_EF800_LOG_REPLAY = 1U << 0,
	VIRTUAL_EF800_LOG_TRUNCATED = 1U << 1,
	VIRTUAL_EF800_LOG_GAP = 1U << 2,
	VIRTUAL_EF800_LOG_EPOCH = 1U << 3,
};

#define VIRTUAL_EF800_LOG_PAYLOAD_HEADER_SIZE 8U

enum virtual_ef800_message_type {
	VIRTUAL_EF800_MSG_HELLO = 1,
	VIRTUAL_EF800_MSG_CAPABILITIES = 2,
	VIRTUAL_EF800_MSG_LCD_FRAME = 3,
	VIRTUAL_EF800_MSG_KEY_DOWN = 4,
	VIRTUAL_EF800_MSG_KEY_UP = 5,
	VIRTUAL_EF800_MSG_KEY_RELEASE_ALL = 6,
	VIRTUAL_EF800_MSG_ACK = 7,
	VIRTUAL_EF800_MSG_STATE = 8,
	VIRTUAL_EF800_MSG_PROFILE_CONTROL = 9,
	VIRTUAL_EF800_MSG_PROFILE_STATE = 10,
	VIRTUAL_EF800_MSG_PLUGIN_UPLOAD_BEGIN = 11,
	VIRTUAL_EF800_MSG_PLUGIN_UPLOAD_CHUNK = 12,
	VIRTUAL_EF800_MSG_PLUGIN_UPLOAD_COMMIT = 13,
	VIRTUAL_EF800_MSG_PLUGIN_STATUS = 14,
	VIRTUAL_EF800_MSG_SIMULATOR_RESTART = 15,
	VIRTUAL_EF800_MSG_DEVICE_DOCK = 16,
	VIRTUAL_EF800_MSG_DEVICE_STATE = 17,
	VIRTUAL_EF800_MSG_PLUGIN_ASSIGN = 18,
	VIRTUAL_EF800_MSG_PLUGIN_EXECUTE = 19,
	VIRTUAL_EF800_MSG_LOG_RECORD = 20,
	VIRTUAL_EF800_MSG_MTP_LIST = 21,
	VIRTUAL_EF800_MSG_MTP_DOWNLOAD = 22,
	VIRTUAL_EF800_MSG_MTP_UPLOAD_BEGIN = 23,
	VIRTUAL_EF800_MSG_MTP_UPLOAD_CHUNK = 24,
	VIRTUAL_EF800_MSG_MTP_UPLOAD_COMMIT = 25,
	VIRTUAL_EF800_MSG_MTP_CANCEL = 26,
	VIRTUAL_EF800_MSG_MTP_RESULT = 27,
	VIRTUAL_EF800_MSG_MTP_PROGRESS = 28,
	VIRTUAL_EF800_MSG_MTP_DOWNLOAD_CHUNK = 29,
	VIRTUAL_EF800_MSG_MTP_LIST_RESULT = 30,
	VIRTUAL_EF800_MSG_MTP_DOWNLOAD_NEXT = 31,
	VIRTUAL_EF800_MSG_LCD_CONTRAST = 32,
	VIRTUAL_EF800_MSG_COMPOSITION_STATE = 33,
	VIRTUAL_EF800_MSG_COMPOSITION_CONTROL = 34,
	VIRTUAL_EF800_MSG_COMPOSITION_RESULT = 35,
};

struct virtual_ef800_log_record {
	uint8_t severity;
	uint8_t flags;
	uint32_t sequence;
	uint16_t text_length;
	const uint8_t *text;
};

struct virtual_ef800_message {
	uint8_t version;
	uint8_t type;
	const uint8_t *payload;
	uint16_t length;
};

/** @brief Encode a typed payload into a bounded virtual protocol frame.
 * @param type Message type identifier.
 * @param payload Payload bytes, or NULL for an empty message.
 * @param length Payload size.
 * @param frame Destination frame buffer.
 * @param frame_size Frame buffer capacity.
 * @return Encoded frame size, or zero when input/capacity is invalid.
 */
size_t virtual_ef800_protocol_encode(uint8_t type, const uint8_t *payload,
					     uint16_t length, uint8_t *frame,
					     size_t frame_size);

/** @brief Validate and decode a framed virtual protocol message.
 * @param frame Complete encoded frame.
 * @param frame_size Available frame bytes.
 * @param message Receives message type and payload view into frame.
 * @return 0 on decode; negative errno for malformed header/length/version.
 */
int virtual_ef800_protocol_decode(const uint8_t *frame, size_t frame_size,
					  struct virtual_ef800_message *message);

/** @brief Encode one structured virtual log record.
 * @param record Severity, sequence, flags, and text view.
 * @param payload Destination payload buffer.
 * @param payload_size Buffer capacity.
 * @return Encoded payload size, or zero when record does not fit.
 */
size_t virtual_ef800_log_encode(const struct virtual_ef800_log_record *record,
					uint8_t *payload, size_t payload_size);

/** @brief Decode a structured virtual log record without copying its text.
 * @param payload Encoded log payload.
 * @param payload_size Payload size.
 * @param record Receives fields and a view of text within payload.
 * @return 0 on decode; negative errno for malformed payload.
 */
int virtual_ef800_log_decode(const uint8_t *payload, size_t payload_size,
					 struct virtual_ef800_log_record *record);

#endif
