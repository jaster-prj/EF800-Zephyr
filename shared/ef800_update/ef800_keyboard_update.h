/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_KEYBOARD_UPDATE_H_
#define EF800_KEYBOARD_UPDATE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ef800_keyboard_package.h"

#define EF800_KB_UPDATE_MAGIC 0x3155504bU /* "KPU1" little-endian */
#define EF800_KB_UPDATE_HEADER_SIZE 16U
#define EF800_KB_UPDATE_CHUNK_MAX 128U
#define EF800_KB_UPDATE_FRAME_HEADER_SIZE 10U
#define EF800_KB_UPDATE_COMMIT 0x04U
#define EF800_KB_UPDATE_ACK 0x06U
#define EF800_KB_UPDATE_NACK 0x15U

/*
 * Wire format (all multi-byte values little-endian):
 *   ready: "KBL" 0x01
 *   begin: magic:u32, target:u8, reserved[3], image_size:u32, crc32:u32
 *   chunk: offset:u32, length:u16, chunk_crc32:u32, data[length]
 *   commit: 0x04; the receiver sends ACK (0x06) after validation/readback.
 * Each begin/chunk is ACKed before the sender continues.
 */

struct ef800_kb_update_header {
	uint8_t target;
	uint32_t image_size;
	uint32_t image_crc32;
};

struct ef800_kb_update_stream {
	uint32_t image_size;
	uint32_t expected_crc32;
	uint32_t received;
	uint32_t crc32;
	uint8_t target;
	bool active;
};

/* Decode the fixed 16-byte wire header and enforce partition/staging bounds. */
bool ef800_kb_update_header_decode(const uint8_t wire[EF800_KB_UPDATE_HEADER_SIZE],
					   struct ef800_kb_update_header *header);

/* Initialize only after a valid target/size header has been received. */
bool ef800_kb_update_stream_begin(struct ef800_kb_update_stream *stream,
					  const struct ef800_kb_update_header *header);

/* Chunks must be contiguous, non-empty, and no larger than the protocol cap. */
bool ef800_kb_update_stream_chunk(struct ef800_kb_update_stream *stream,
					  uint32_t offset, const uint8_t *data,
					  size_t length);

/* Finish validates exact length and whole-image CRC, then consumes the state. */
bool ef800_kb_update_stream_finish(struct ef800_kb_update_stream *stream);

#endif /* EF800_KEYBOARD_UPDATE_H_ */
