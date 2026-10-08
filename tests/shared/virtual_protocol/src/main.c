/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

#include "ef800_virtual_protocol.h"

ZTEST(shared_virtual_protocol, test_round_trip_and_fragment_size)
{
	static const uint8_t payload[] = {'A', 'R', 'R', 'O', 'W', 'D', 'O', 'W', 'N'};
	uint8_t frame[64];
	struct ef800_virtual_message message;
	size_t length;

	length = ef800_virtual_protocol_encode(EF800_VIRTUAL_MSG_KEY_DOWN,
		payload, sizeof(payload), frame, sizeof(frame));
	zassert_equal(length, EF800_VIRTUAL_PROTOCOL_HEADER_SIZE + sizeof(payload), NULL);
	zassert_equal(ef800_virtual_protocol_decode(frame, length, &message), 0, NULL);
	zassert_equal(message.type, EF800_VIRTUAL_MSG_KEY_DOWN, NULL);
	zassert_equal(message.length, sizeof(payload), NULL);
	zassert_mem_equal(message.payload, payload, sizeof(payload), NULL);
	zassert_equal(ef800_virtual_protocol_decode(frame, length - 1U, &message),
		-EMSGSIZE, NULL);
}

ZTEST(shared_virtual_protocol, test_rejects_bad_magic_version_and_bounds)
{
	uint8_t frame[EF800_VIRTUAL_PROTOCOL_HEADER_SIZE + 1U];
	struct ef800_virtual_message message;
	uint8_t payload = 1U;

	zassert_equal(ef800_virtual_protocol_encode(EF800_VIRTUAL_MSG_ACK, &payload,
		1U, frame, sizeof(frame)), sizeof(frame), NULL);
	frame[0] ^= 1U;
	zassert_equal(ef800_virtual_protocol_decode(frame, sizeof(frame), &message),
		-EINVAL, NULL);
	frame[0] ^= 1U;
	frame[4]++;
	zassert_equal(ef800_virtual_protocol_decode(frame, sizeof(frame), &message),
		-EINVAL, NULL);
	zassert_equal(ef800_virtual_protocol_encode(EF800_VIRTUAL_MSG_ACK, NULL,
		EF800_VIRTUAL_PROTOCOL_MAX_PAYLOAD + 1U, frame, sizeof(frame)), 0U, NULL);
}

ZTEST(shared_virtual_protocol, test_multiple_frames_and_fragmented_accumulation)
{
	uint8_t first[64];
	uint8_t second[64];
	uint8_t stream[128];
	struct ef800_virtual_message message;
	size_t first_length;
	size_t second_length;

	first_length = ef800_virtual_protocol_encode(EF800_VIRTUAL_MSG_KEY_DOWN,
		(const uint8_t *)"up", 2U, first, sizeof(first));
	second_length = ef800_virtual_protocol_encode(EF800_VIRTUAL_MSG_KEY_UP,
		(const uint8_t *)"up", 2U, second, sizeof(second));
	memcpy(stream, first, first_length);
	memcpy(&stream[first_length], second, second_length);
	zassert_equal(ef800_virtual_protocol_decode(stream, first_length, &message),
		0, NULL);
	zassert_equal(message.type, EF800_VIRTUAL_MSG_KEY_DOWN, NULL);
	zassert_equal(ef800_virtual_protocol_decode(&stream[first_length], second_length,
		&message), 0, NULL);
	zassert_equal(message.type, EF800_VIRTUAL_MSG_KEY_UP, NULL);
}

ZTEST(shared_virtual_protocol, test_log_record_round_trip_and_bounds)
{
	static const uint8_t text[] = "Plugin discovery found 2 package(s)";
	uint8_t payload[EF800_VIRTUAL_LOG_PAYLOAD_HEADER_SIZE +
		EF800_VIRTUAL_LOG_MAX_TEXT];
	struct ef800_virtual_log_record input = {
		.severity = EF800_VIRTUAL_LOG_WARNING,
		.flags = EF800_VIRTUAL_LOG_REPLAY | EF800_VIRTUAL_LOG_TRUNCATED,
		.sequence = 0x12345678U,
		.text_length = sizeof(text) - 1U,
		.text = text,
	};
	struct ef800_virtual_log_record output;
	size_t length = ef800_virtual_log_encode(&input, payload, sizeof(payload));

	zassert_equal(length, EF800_VIRTUAL_LOG_PAYLOAD_HEADER_SIZE +
		input.text_length, NULL);
	zassert_equal(ef800_virtual_log_decode(payload, length, &output), 0, NULL);
	zassert_equal(output.severity, input.severity, NULL);
	zassert_equal(output.flags, input.flags, NULL);
	zassert_equal(output.sequence, input.sequence, NULL);
	zassert_equal(output.text_length, input.text_length, NULL);
	zassert_mem_equal(output.text, input.text, input.text_length, NULL);
	zassert_equal(ef800_virtual_log_decode(payload, length - 1U, &output),
		-EINVAL, NULL);
}

ZTEST(shared_virtual_protocol, test_log_record_rejects_invalid_metadata)
{
	uint8_t payload[EF800_VIRTUAL_LOG_PAYLOAD_HEADER_SIZE];
	struct ef800_virtual_log_record record = {
		.severity = EF800_VIRTUAL_LOG_INFO,
		.flags = 0U,
		.sequence = 1U,
		.text_length = 0U,
		.text = NULL,
	};
	struct ef800_virtual_log_record decoded;

	zassert_equal(ef800_virtual_log_encode(&record, payload, sizeof(payload)),
		EF800_VIRTUAL_LOG_PAYLOAD_HEADER_SIZE, NULL);
	payload[0] = EF800_VIRTUAL_LOG_PANIC + 1U;
	zassert_equal(ef800_virtual_log_decode(payload, sizeof(payload), &decoded),
		-EINVAL, NULL);
	payload[0] = EF800_VIRTUAL_LOG_INFO;
	payload[1] = 0x80U;
	zassert_equal(ef800_virtual_log_decode(payload, sizeof(payload), &decoded),
		-EINVAL, NULL);
}

ZTEST_SUITE(shared_virtual_protocol, NULL, NULL, NULL, NULL, NULL);
