/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>

#include <zephyr/ztest.h>

#include "ef800_keyboard_update.h"
#undef EF800_KB_UPDATE_ACK
#undef EF800_KB_UPDATE_NACK
#include "ef800_keyboard_update_protocol.h"
#include "ef800_update.h"
#include "ef800_keyboard_update_service.h"
#include "ef800_keyboard_i2c.h"

static void put_le32(uint8_t *p, uint32_t value)
{
	p[0] = value & 0xffU;
	p[1] = (value >> 8) & 0xffU;
	p[2] = (value >> 16) & 0xffU;
	p[3] = (value >> 24) & 0xffU;
}

static void make_header(uint8_t wire[EF800_KB_UPDATE_HEADER_SIZE],
			uint8_t target, uint32_t size, uint32_t crc)
{
	memset(wire, 0, EF800_KB_UPDATE_HEADER_SIZE);
	put_le32(&wire[0], EF800_KB_UPDATE_MAGIC);
	wire[4] = target;
	put_le32(&wire[8], size);
	put_le32(&wire[12], crc);
}

ZTEST(ef800_keyboard_update, test_image_kind_selects_bounded_update_plan)
{
	struct ef800_keyboard_update_plan plan;

	zassert_ok(ef800_keyboard_update_plan_select(
		EF800_KEYBOARD_IMAGE_APPLICATION, &plan), NULL);
	zassert_equal(plan.image_base, EF800_KEYBOARD_PACKAGE_BASE, NULL);
	zassert_equal(plan.image_limit, EF800_KEYBOARD_PACKAGE_LIMIT, NULL);
	zassert_equal(plan.target, EF800_KB_UPDATE_TARGET_APPLICATION, NULL);
	zassert_equal(plan.service_address, EF800_KEYBOARD_UPDATER_I2C_ADDRESS,
		NULL);
	zassert_ok(ef800_keyboard_update_plan_select(
		EF800_KEYBOARD_IMAGE_BOOTLOADER, &plan), NULL);
	zassert_equal(plan.image_base, EF800_KEYBOARD_BOOT_BASE, NULL);
	zassert_equal(plan.image_limit, EF800_KEYBOARD_BOOT_LIMIT, NULL);
	zassert_equal(plan.target, EF800_KB_UPDATE_TARGET_BOOTLOADER, NULL);
	zassert_ok(ef800_keyboard_update_plan_select(
		EF800_KEYBOARD_IMAGE_UPDATER, &plan), NULL);
	zassert_equal(plan.image_base, EF800_KEYBOARD_UPDATER_BASE, NULL);
	zassert_equal(plan.image_limit, EF800_KEYBOARD_UPDATER_LIMIT, NULL);
	zassert_equal(plan.target, EF800_KB_UPDATE_TARGET_UPDATER, NULL);
	zassert_equal(plan.service_address, EF800_KEYBOARD_I2C_ADDRESS, NULL);
	zassert_equal(ef800_keyboard_update_plan_select(0U, &plan), -EINVAL, NULL);
	zassert_equal(ef800_keyboard_update_plan_select(
		EF800_KEYBOARD_IMAGE_APPLICATION, NULL), -EINVAL, NULL);
}

ZTEST(ef800_keyboard_update, test_transaction_requires_complete_adapter_context)
{
	struct ef800_keyboard_update_transaction transaction = {0};

	zassert_equal(ef800_keyboard_update_transaction_run(NULL), -EINVAL, NULL);
	zassert_equal(ef800_keyboard_update_transaction_run(&transaction), -EINVAL,
		NULL);
}

ZTEST(ef800_keyboard_update, test_valid_targets_decode_with_fixed_sizes)
{
	uint8_t wire[EF800_KB_UPDATE_HEADER_SIZE];
	struct ef800_kb_update_header header;

	make_header(wire, EF800_KEYBOARD_IMAGE_APPLICATION,
		EF800_KEYBOARD_PACKAGE_LIMIT, 0x12345678U);
	zassert_true(ef800_kb_update_header_decode(wire, &header), NULL);
	zassert_equal(header.target, EF800_KEYBOARD_IMAGE_APPLICATION, NULL);
	zassert_equal(header.image_size, EF800_KEYBOARD_PACKAGE_LIMIT, NULL);

	make_header(wire, EF800_KEYBOARD_IMAGE_BOOTLOADER,
		EF800_KEYBOARD_BOOT_LIMIT, 0U);
	zassert_true(ef800_kb_update_header_decode(wire, &header), NULL);
	zassert_equal(header.target, EF800_KEYBOARD_IMAGE_BOOTLOADER, NULL);
}

ZTEST(ef800_keyboard_update, test_malformed_header_target_and_bounds_rejected)
{
	uint8_t wire[EF800_KB_UPDATE_HEADER_SIZE];
	struct ef800_kb_update_header header;

	make_header(wire, 0U, 32U, 0U);
	zassert_false(ef800_kb_update_header_decode(wire, &header), NULL);
	make_header(wire, EF800_KEYBOARD_IMAGE_BOOTLOADER,
		EF800_KEYBOARD_BOOT_LIMIT + 1U, 0U);
	zassert_false(ef800_kb_update_header_decode(wire, &header), NULL);
	make_header(wire, EF800_KEYBOARD_IMAGE_APPLICATION,
		EF800_KEYBOARD_PACKAGE_LIMIT + 1U, 0U);
	zassert_false(ef800_kb_update_header_decode(wire, &header), NULL);
	make_header(wire, EF800_KEYBOARD_IMAGE_APPLICATION, 1U, 0U);
	wire[7] = 1U;
	zassert_false(ef800_kb_update_header_decode(wire, &header), NULL);
}

ZTEST(ef800_keyboard_update, test_ordered_chunks_and_crc_required)
{
	static const uint8_t image[] = {0x01U, 0x02U, 0x03U, 0x04U, 0x05U};
	struct ef800_kb_update_header header = {
		.target = EF800_KEYBOARD_IMAGE_APPLICATION,
		.image_size = sizeof(image),
		.image_crc32 = ef800_crc32(image, sizeof(image)),
	};
	struct ef800_kb_update_stream stream;

	zassert_true(ef800_kb_update_stream_begin(&stream, &header), NULL);
	zassert_false(ef800_kb_update_stream_chunk(&stream, 1U, image, 2U), NULL);
	zassert_equal(stream.received, 0U, NULL);
	zassert_true(ef800_kb_update_stream_chunk(&stream, 0U, image, 2U), NULL);
	zassert_false(ef800_kb_update_stream_chunk(&stream, 2U, image + 2U,
		EF800_KB_UPDATE_CHUNK_MAX + 1U), NULL);
	zassert_true(ef800_kb_update_stream_chunk(&stream, 2U, image + 2U, 3U), NULL);
	zassert_true(ef800_kb_update_stream_finish(&stream), NULL);
	zassert_false(stream.active, NULL);

	header.image_crc32 ^= 1U;
	zassert_true(ef800_kb_update_stream_begin(&stream, &header), NULL);
	zassert_true(ef800_kb_update_stream_chunk(&stream, 0U, image,
		sizeof(image)), NULL);
	zassert_false(ef800_kb_update_stream_finish(&stream), NULL);
}

ZTEST(ef800_keyboard_update, test_interrupted_application_stream_fails_closed)
{
	static const uint8_t payload[] = {0xaaU, 0xbbU, 0xccU};
	struct ef800_kb_update_header header = {
		.target = EF800_KEYBOARD_IMAGE_APPLICATION,
		.image_size = sizeof(payload),
		.image_crc32 = ef800_crc32(payload, sizeof(payload)),
	};
	struct ef800_kb_update_stream stream;

	zassert_true(ef800_kb_update_stream_begin(&stream, &header), NULL);
	zassert_true(ef800_kb_update_stream_chunk(&stream, 0U, payload, 2U), NULL);
	zassert_false(ef800_kb_update_stream_finish(&stream), NULL);
	/* A fresh BEGIN resets the interrupted transaction for a clean retry. */
	zassert_true(ef800_kb_update_stream_begin(&stream, &header), NULL);
	zassert_equal(stream.received, 0U, NULL);
	zassert_equal(stream.crc32, 0U, NULL);
	zassert_true(ef800_kb_update_stream_chunk(&stream, 0U, payload,
		sizeof(payload)), NULL);
	zassert_true(ef800_kb_update_stream_finish(&stream), NULL);
}

ZTEST(ef800_keyboard_update, test_update_service_recovery_authorization)
{
	/* The resident updater remains available to repair either replaceable image. */
	zassert_true(ef800_kb_update_service_target_valid(
		EF800_KB_UPDATE_SERVICE_UPDATER,
		EF800_KB_UPDATE_TARGET_APPLICATION), NULL);
	zassert_true(ef800_kb_update_service_target_valid(
		EF800_KB_UPDATE_SERVICE_UPDATER,
		EF800_KB_UPDATE_TARGET_BOOTLOADER), NULL);
	/* Neither service may rewrite the image it is executing. */
	zassert_false(ef800_kb_update_service_target_valid(
		EF800_KB_UPDATE_SERVICE_UPDATER,
		EF800_KB_UPDATE_TARGET_UPDATER), NULL);
	zassert_false(ef800_kb_update_service_target_valid(
		EF800_KB_UPDATE_SERVICE_KEYBOARD_APPLICATION,
		EF800_KB_UPDATE_TARGET_APPLICATION), NULL);
	zassert_false(ef800_kb_update_service_target_valid(
		EF800_KB_UPDATE_SERVICE_KEYBOARD_APPLICATION,
		EF800_KB_UPDATE_TARGET_BOOTLOADER), NULL);
	zassert_true(ef800_kb_update_service_target_valid(
		EF800_KB_UPDATE_SERVICE_KEYBOARD_APPLICATION,
		EF800_KB_UPDATE_TARGET_UPDATER), NULL);
}

ZTEST(ef800_keyboard_update, test_custom_command_frames_and_timeout)
{
	uint8_t frame[] = {EF800_KB_UPDATE_CMD_WRITE_MEMORY, 0U, 0x12U};
	uint8_t encoded[8];
	uint8_t ack[] = {EF800_KB_PROTOCOL_ACK, 0x12U, 0x6BU};
	uint8_t nack[] = {EF800_KB_PROTOCOL_NACK};
	uint8_t busy[] = {EF800_KB_PROTOCOL_BUSY};
	uint8_t version[EF800_KB_UPDATE_VERSION_SIZE];

	zassert_equal(EF800_KB_PROTOCOL_ACK, 0x79U, NULL);
	zassert_equal(EF800_KB_PROTOCOL_NACK, 0x1FU, NULL);
	zassert_equal(EF800_KB_PROTOCOL_BUSY, 0x76U, NULL);
	zassert_equal(ef800_kb_update_version_encode(
		EF800_KB_UPDATE_VERSION_COMMAND, EF800_KB_UPDATE_VERSION_ACK,
		1U, 2U, 3U, version, sizeof(version)), sizeof(version), NULL);
	zassert_equal(version[0], 0xF2U, NULL);
	zassert_equal(version[1], 1U, NULL);
	zassert_equal(version[2], 2U, NULL);
	zassert_equal(version[3], 3U, NULL);
	zassert_equal(version[4], 0x06U, NULL);
	zassert_equal(ef800_kb_update_version_encode(
		EF800_KB_UPDATE_VERSION_COMMAND, EF800_KB_UPDATE_VERSION_ACK,
		1U, 2U, 3U, version, sizeof(version) - 1U), 0U, NULL);
	frame[1] = (uint8_t)~frame[0];
	frame[2] = ef800_kb_update_checksum(frame, 2U);
	zassert_true(ef800_kb_update_frame_valid(frame, sizeof(frame)), NULL);
	frame[1] ^= 1U;
	zassert_false(ef800_kb_update_frame_valid(frame, sizeof(frame)), NULL);
	zassert_equal(ef800_kb_update_frame_encode(0x31U, &frame[2], 1U,
		encoded, sizeof(encoded)), 4U, NULL);
	zassert_equal(encoded[0], 0x31U, NULL);
	zassert_equal(encoded[1], 0xCEU, NULL);
	zassert_true(ef800_kb_update_frame_valid(encoded, 4U), NULL);
	zassert_true(ef800_kb_update_response_valid(ack, sizeof(ack)), NULL);
	zassert_true(ef800_kb_update_response_valid(nack, sizeof(nack)), NULL);
	zassert_true(ef800_kb_update_response_valid(busy, sizeof(busy)), NULL);
	ack[2] ^= 1U;
	zassert_false(ef800_kb_update_response_valid(ack, sizeof(ack)), NULL);
	zassert_false(ef800_kb_update_response_valid(busy, 2U), NULL);
	zassert_false(ef800_kb_update_frame_timing_valid(100U,
		100U + EF800_KB_UPDATE_FRAME_TIMEOUT_MS + 1U), NULL);
	zassert_true(ef800_kb_update_frame_timing_valid(UINT32_MAX - 100U,
		899U), NULL);
}

ZTEST(ef800_keyboard_update, test_package_targets_match_partition_map)
{
	struct ef800_keyboard_package_header header = {
		.magic = EF800_KEYBOARD_PACKAGE_MAGIC,
		.format = EF800_KEYBOARD_PACKAGE_FORMAT,
		.header_size = EF800_KEYBOARD_PACKAGE_HEADER_SIZE,
		.target_id = EF800_KEYBOARD_PACKAGE_TARGET,
		.image_base = EF800_KEYBOARD_PACKAGE_BASE,
		.image_size = EF800_KEYBOARD_PACKAGE_LIMIT,
		.image_kind = EF800_KEYBOARD_IMAGE_APPLICATION,
	};

	zassert_true(ef800_keyboard_package_valid(&header,
		EF800_KEYBOARD_PACKAGE_HEADER_SIZE + header.image_size), NULL);
	header.image_kind = EF800_KEYBOARD_IMAGE_BOOTLOADER;
	header.image_base = EF800_KEYBOARD_BOOT_BASE;
	header.image_size = EF800_KEYBOARD_BOOT_LIMIT;
	zassert_true(ef800_keyboard_package_valid(&header,
		EF800_KEYBOARD_PACKAGE_HEADER_SIZE + header.image_size), NULL);
	header.image_kind = EF800_KEYBOARD_IMAGE_UPDATER;
	header.image_base = EF800_KEYBOARD_UPDATER_BASE;
	header.image_size = EF800_KEYBOARD_UPDATER_LIMIT;
	zassert_true(ef800_keyboard_package_valid(&header,
		EF800_KEYBOARD_PACKAGE_HEADER_SIZE + header.image_size), NULL);
	header.image_base = EF800_KEYBOARD_PACKAGE_BASE;
	zassert_false(ef800_keyboard_package_valid(&header,
		EF800_KEYBOARD_PACKAGE_HEADER_SIZE + header.image_size), NULL);
}

ZTEST(ef800_keyboard_update, test_update_service_little_endian_helpers)
{
	uint8_t bytes[4];

	ef800_keyboard_update_put_le16(bytes, UINT16_C(0xA17C));
	zassert_equal(bytes[0], 0x7CU, NULL);
	zassert_equal(bytes[1], 0xA1U, NULL);
	ef800_keyboard_update_put_le32(bytes, UINT32_C(0x78563412));
	zassert_equal(bytes[0], 0x12U, NULL);
	zassert_equal(bytes[1], 0x34U, NULL);
	zassert_equal(bytes[2], 0x56U, NULL);
	zassert_equal(bytes[3], 0x78U, NULL);
	zassert_equal(ef800_keyboard_update_get_le32(bytes),
		UINT32_C(0x78563412), NULL);
}

ZTEST(ef800_keyboard_update, test_fixed_partition_page_and_write_bounds)
{
	zassert_true(ef800_kb_update_page_valid(
		EF800_KB_UPDATE_TARGET_BOOTLOADER, 0U), NULL);
	zassert_true(ef800_kb_update_page_valid(
		EF800_KB_UPDATE_TARGET_BOOTLOADER, 15U), NULL);
	zassert_false(ef800_kb_update_page_valid(
		EF800_KB_UPDATE_TARGET_BOOTLOADER, 16U), NULL);
	zassert_true(ef800_kb_update_page_valid(
		EF800_KB_UPDATE_TARGET_APPLICATION, 16U), NULL);
	zassert_true(ef800_kb_update_page_valid(
		EF800_KB_UPDATE_TARGET_APPLICATION, 111U), NULL);
	zassert_false(ef800_kb_update_page_valid(
		EF800_KB_UPDATE_TARGET_APPLICATION, 112U), NULL);
	zassert_true(ef800_kb_update_page_valid(
		EF800_KB_UPDATE_TARGET_UPDATER, 112U), NULL);
	zassert_true(ef800_kb_update_page_valid(
		EF800_KB_UPDATE_TARGET_UPDATER, 127U), NULL);
	zassert_false(ef800_kb_update_page_valid(
		EF800_KB_UPDATE_TARGET_UPDATER, 128U), NULL);
	zassert_false(ef800_kb_update_page_valid(
		(enum ef800_kb_update_target)99, 0U), NULL);

	zassert_true(ef800_kb_update_write_valid(
		EF800_KB_UPDATE_TARGET_APPLICATION, 0x08008000U, 256U), NULL);
	zassert_false(ef800_kb_update_write_valid(
		EF800_KB_UPDATE_TARGET_APPLICATION, 0x08007FF8U, 16U), NULL);
	zassert_false(ef800_kb_update_write_valid(
		EF800_KB_UPDATE_TARGET_APPLICATION, 0x08008001U, 8U), NULL);
	zassert_false(ef800_kb_update_write_valid(
		EF800_KB_UPDATE_TARGET_APPLICATION, 0x08008000U, 7U), NULL);
	zassert_false(ef800_kb_update_write_valid(
		EF800_KB_UPDATE_TARGET_APPLICATION, 0x08008000U, 264U), NULL);
	zassert_true(ef800_kb_update_write_valid(
		EF800_KB_UPDATE_TARGET_UPDATER, 0x08038000U, 8U), NULL);
	zassert_false(ef800_kb_update_service_target_valid(
		EF800_KB_UPDATE_SERVICE_UPDATER,
		EF800_KB_UPDATE_TARGET_UPDATER), NULL);
	zassert_true(ef800_kb_update_service_target_valid(
		EF800_KB_UPDATE_SERVICE_UPDATER,
		EF800_KB_UPDATE_TARGET_APPLICATION), NULL);
	zassert_true(ef800_kb_update_service_target_valid(
		EF800_KB_UPDATE_SERVICE_KEYBOARD_APPLICATION,
		EF800_KB_UPDATE_TARGET_UPDATER), NULL);
	zassert_false(ef800_kb_update_service_target_valid(
		EF800_KB_UPDATE_SERVICE_KEYBOARD_APPLICATION,
		EF800_KB_UPDATE_TARGET_APPLICATION), NULL);
}

ZTEST_SUITE(ef800_keyboard_update, NULL, NULL, NULL, NULL, NULL);
