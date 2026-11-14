/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include "ef800_firmware_package.h"
#include "ef800_update.h"

#define TEST_IMAGE_SIZE 16U
#define TEST_PACKAGE_SIZE (EF800_FIRMWARE_PACKAGE_HEADER_SIZE + TEST_IMAGE_SIZE)

static uint8_t package[TEST_PACKAGE_SIZE];
static const struct ef800_firmware_semver coordinator_version = { 1U, 0U, 0U };

static void make_package(void)
{
	memset(package, 0, sizeof(package));
	sys_put_le32(EF800_FIRMWARE_PACKAGE_MAGIC, &package[0]);
	sys_put_le16(EF800_FIRMWARE_PACKAGE_FORMAT_VERSION, &package[4]);
	sys_put_le16(EF800_FIRMWARE_PACKAGE_HEADER_SIZE, &package[6]);
	sys_put_le16(EF800_FIRMWARE_TARGET_APPLICATION, &package[8]);
	sys_put_le16(EF800_FIRMWARE_COORDINATOR_BOOTLOADER, &package[10]);
	sys_put_le16(EF800_FIRMWARE_HARDWARE_ID_L496, &package[12]);
	sys_put_le16(1U, &package[14]);
	sys_put_le16(0U, &package[16]);
	sys_put_le16(0U, &package[18]);
	sys_put_le16(1U, &package[20]);
	sys_put_le16(0U, &package[22]);
	sys_put_le16(0U, &package[24]);
	sys_put_le32(EF800_FIRMWARE_L496_APPLICATION_ADDRESS, &package[26]);
	sys_put_le32(TEST_IMAGE_SIZE, &package[30]);
	for (size_t index = 0U; index < TEST_IMAGE_SIZE; index++) {
		package[EF800_FIRMWARE_PACKAGE_HEADER_SIZE + index] =
			(uint8_t)(0xa0U + index);
	}
	sys_put_le32(ef800_crc32(&package[EF800_FIRMWARE_PACKAGE_HEADER_SIZE],
				 TEST_IMAGE_SIZE), &package[34]);
	sys_put_le32(0U, &package[EF800_FIRMWARE_PACKAGE_HEADER_CRC_OFFSET]);
	sys_put_le32(ef800_crc32(package, EF800_FIRMWARE_PACKAGE_HEADER_SIZE),
		     &package[EF800_FIRMWARE_PACKAGE_HEADER_CRC_OFFSET]);
}

static int parse_application(const uint8_t *data, size_t size,
			     struct ef800_firmware_package_header *header,
			     const uint8_t **image)
{
	return ef800_firmware_package_parse(
		data, size, EF800_FIRMWARE_TARGET_APPLICATION,
		EF800_FIRMWARE_HARDWARE_ID_L496, &coordinator_version, header, image);
}

static void make_target_header(
	uint8_t wire[EF800_FIRMWARE_PACKAGE_HEADER_SIZE],
	enum ef800_firmware_target_id target, uint16_t coordinator_id,
	uint16_t hardware_id, uint32_t load_address, uint32_t image_size)
{
	memset(wire, 0, EF800_FIRMWARE_PACKAGE_HEADER_SIZE);
	sys_put_le32(EF800_FIRMWARE_PACKAGE_MAGIC, &wire[0]);
	sys_put_le16(EF800_FIRMWARE_PACKAGE_FORMAT_VERSION, &wire[4]);
	sys_put_le16(EF800_FIRMWARE_PACKAGE_HEADER_SIZE, &wire[6]);
	sys_put_le16(target, &wire[8]);
	sys_put_le16(coordinator_id, &wire[10]);
	sys_put_le16(hardware_id, &wire[12]);
	sys_put_le16(1U, &wire[14]);
	sys_put_le16(1U, &wire[20]);
	sys_put_le32(load_address, &wire[26]);
	sys_put_le32(image_size, &wire[30]);
	sys_put_le32(0U, &wire[EF800_FIRMWARE_PACKAGE_HEADER_CRC_OFFSET]);
	sys_put_le32(ef800_crc32(wire, EF800_FIRMWARE_PACKAGE_HEADER_SIZE),
		     &wire[EF800_FIRMWARE_PACKAGE_HEADER_CRC_OFFSET]);
}

ZTEST(ef800_firmware_package, test_valid_package_decodes_header_and_payload)
{
	struct ef800_firmware_package_header header;
	const uint8_t *image;

	make_package();
	zassert_ok(parse_application(package, sizeof(package), &header, &image));
	zassert_equal(header.target_id, EF800_FIRMWARE_TARGET_APPLICATION);
	zassert_equal(header.load_address,
		      EF800_FIRMWARE_L496_APPLICATION_ADDRESS);
	zassert_equal(header.image_size, TEST_IMAGE_SIZE);
	zassert_equal_ptr(image, &package[EF800_FIRMWARE_PACKAGE_HEADER_SIZE]);
}

ZTEST(ef800_firmware_package, test_short_unknown_and_unsupported_headers_rejected)
{
	struct ef800_firmware_package_header header;
	const uint8_t *image;

	make_package();
	zassert_equal(parse_application(package, EF800_FIRMWARE_PACKAGE_HEADER_SIZE - 1U,
					&header, &image), -EINVAL);
	package[0] ^= 0x01U;
	zassert_equal(parse_application(package, sizeof(package), &header, &image),
		      -EPROTONOSUPPORT);
	make_package();
	sys_put_le16(99U, &package[4]);
	zassert_equal(parse_application(package, sizeof(package), &header, &image),
		      -EPROTONOSUPPORT);
}

ZTEST(ef800_firmware_package, test_wrong_target_hardware_and_address_rejected)
{
	struct ef800_firmware_package_header header;
	const uint8_t *image;

	make_package();
	zassert_equal(ef800_firmware_package_parse(
		package, sizeof(package), EF800_FIRMWARE_TARGET_BACKUP,
		EF800_FIRMWARE_HARDWARE_ID_L496, &coordinator_version, &header, &image),
		      -EXDEV);
	make_package();
	zassert_equal(ef800_firmware_package_parse(
		package, sizeof(package), EF800_FIRMWARE_TARGET_APPLICATION,
		EF800_FIRMWARE_HARDWARE_ID_L431, &coordinator_version, &header, &image),
		      -EXDEV);
	make_package();
	sys_put_le32(EF800_FIRMWARE_L496_BACKUP_ADDRESS, &package[26]);
	zassert_equal(parse_application(package, sizeof(package), &header, &image),
		      -EXDEV);
}

ZTEST(ef800_firmware_package, test_incompatible_coordinator_rejected)
{
	struct ef800_firmware_package_header header;
	const uint8_t *image;
	const struct ef800_firmware_semver old_coordinator = { 0U, 9U, 9U };

	make_package();
	zassert_equal(ef800_firmware_package_parse(
		package, sizeof(package), EF800_FIRMWARE_TARGET_APPLICATION,
		EF800_FIRMWARE_HARDWARE_ID_L496, &old_coordinator, &header, &image),
		      -EPROTONOSUPPORT);
}

ZTEST(ef800_firmware_package, test_corrupt_crc_reserved_and_file_bounds_rejected)
{
	struct ef800_firmware_package_header header;
	const uint8_t *image;

	make_package();
	package[EF800_FIRMWARE_PACKAGE_HEADER_SIZE] ^= 1U;
	zassert_equal(parse_application(package, sizeof(package), &header, &image),
		      -EBADMSG);
	make_package();
	package[EF800_FIRMWARE_PACKAGE_RESERVED_OFFSET] = 1U;
	zassert_equal(parse_application(package, sizeof(package), &header, &image),
		      -EBADMSG);
	make_package();
	zassert_equal(parse_application(package, sizeof(package) - 1U, &header, &image),
		      -EMSGSIZE);
	make_package();
	sys_put_le32(EF800_FIRMWARE_L496_APPLICATION_SIZE + 1U, &package[30]);
	zassert_equal(parse_application(package, sizeof(package), &header, &image),
		      -EMSGSIZE);
}

ZTEST(ef800_firmware_package, test_target_set_rejects_unknown_and_duplicate_ids)
{
	const uint16_t unique[] = {
		EF800_FIRMWARE_TARGET_APPLICATION,
		EF800_FIRMWARE_TARGET_BACKUP,
		EF800_FIRMWARE_TARGET_KEYBOARD_APPLICATION,
	};
	const uint16_t duplicate[] = {
		EF800_FIRMWARE_TARGET_APPLICATION,
		EF800_FIRMWARE_TARGET_APPLICATION,
	};
	const uint16_t unknown[] = { 0U };

	zassert_ok(ef800_firmware_package_set_validate(unique, ARRAY_SIZE(unique)));
	zassert_equal(ef800_firmware_package_set_validate(
		duplicate, ARRAY_SIZE(duplicate)), -EEXIST);
	zassert_equal(ef800_firmware_package_set_validate(
		unknown, ARRAY_SIZE(unknown)), -EINVAL);
	zassert_ok(ef800_firmware_package_set_validate(NULL, 0U));
}

ZTEST(ef800_firmware_package, test_all_six_target_maps_and_partition_limits)
{
	static const struct {
		enum ef800_firmware_target_id target;
		uint16_t coordinator;
		uint16_t hardware;
		uint32_t address;
		uint32_t limit;
	} mappings[] = {
		{ EF800_FIRMWARE_TARGET_BOOTLOADER,
		  EF800_FIRMWARE_COORDINATOR_APPLICATION,
		  EF800_FIRMWARE_HARDWARE_ID_L496,
		  EF800_FIRMWARE_L496_BOOTLOADER_ADDRESS,
		  EF800_FIRMWARE_L496_BOOTLOADER_SIZE },
		{ EF800_FIRMWARE_TARGET_BACKUP,
		  EF800_FIRMWARE_COORDINATOR_APPLICATION,
		  EF800_FIRMWARE_HARDWARE_ID_L496,
		  EF800_FIRMWARE_L496_BACKUP_ADDRESS,
		  EF800_FIRMWARE_L496_BACKUP_SIZE },
		{ EF800_FIRMWARE_TARGET_APPLICATION,
		  EF800_FIRMWARE_COORDINATOR_BOOTLOADER,
		  EF800_FIRMWARE_HARDWARE_ID_L496,
		  EF800_FIRMWARE_L496_APPLICATION_ADDRESS,
		  EF800_FIRMWARE_L496_APPLICATION_SIZE },
		{ EF800_FIRMWARE_TARGET_KEYBOARD_UPDATER,
		  EF800_FIRMWARE_COORDINATOR_APPLICATION,
		  EF800_FIRMWARE_HARDWARE_ID_L431,
		  EF800_FIRMWARE_L431_UPDATER_ADDRESS,
		  EF800_FIRMWARE_L431_UPDATER_SIZE },
		{ EF800_FIRMWARE_TARGET_KEYBOARD_BOOTLOADER,
		  EF800_FIRMWARE_COORDINATOR_APPLICATION,
		  EF800_FIRMWARE_HARDWARE_ID_L431,
		  EF800_FIRMWARE_L431_BOOTLOADER_ADDRESS,
		  EF800_FIRMWARE_L431_BOOTLOADER_SIZE },
		{ EF800_FIRMWARE_TARGET_KEYBOARD_APPLICATION,
		  EF800_FIRMWARE_COORDINATOR_APPLICATION,
		  EF800_FIRMWARE_HARDWARE_ID_L431,
		  EF800_FIRMWARE_L431_APPLICATION_ADDRESS,
		  EF800_FIRMWARE_L431_APPLICATION_SIZE },
	};
	uint8_t wire[EF800_FIRMWARE_PACKAGE_HEADER_SIZE];
	struct ef800_firmware_package_header parsed;

	for (size_t i = 0U; i < ARRAY_SIZE(mappings); ++i) {
		const struct ef800_firmware_semver coordinator = { 1U, 0U, 0U };
		size_t package_size = EF800_FIRMWARE_PACKAGE_HEADER_SIZE +
			(size_t)mappings[i].limit;

		make_target_header(wire, mappings[i].target, mappings[i].coordinator,
				   mappings[i].hardware, mappings[i].address,
				   mappings[i].limit);
		zassert_ok(ef800_firmware_package_header_parse(
			wire, sizeof(wire), package_size, mappings[i].target,
			mappings[i].hardware, &coordinator, &parsed),
			"valid map rejected for target %u", mappings[i].target);
		zassert_equal(parsed.load_address, mappings[i].address, NULL);
		zassert_equal(parsed.image_size, mappings[i].limit, NULL);

		make_target_header(wire, mappings[i].target, mappings[i].coordinator,
				   mappings[i].hardware, mappings[i].address,
				   mappings[i].limit + 1U);
		zassert_equal(ef800_firmware_package_header_parse(
			wire, sizeof(wire), EF800_FIRMWARE_PACKAGE_HEADER_SIZE +
				(size_t)mappings[i].limit + 1U,
			mappings[i].target, mappings[i].hardware, &coordinator,
			&parsed), -EMSGSIZE,
			"partition overflow accepted for target %u",
			mappings[i].target);
	}
}

ZTEST(ef800_firmware_package, test_semver_exact_match_policy)
{
	const struct ef800_firmware_semver running = { 1U, 2U, 3U };
	const struct ef800_firmware_semver matching = { 1U, 2U, 3U };
	const struct ef800_firmware_semver different_patch = { 1U, 2U, 4U };

	zassert_true(ef800_firmware_semver_equal(&running, &matching), NULL);
	zassert_false(ef800_firmware_semver_equal(&running, &different_patch),
		      NULL);
	zassert_false(ef800_firmware_semver_equal(&running, NULL), NULL);
	zassert_ok(ef800_firmware_application_update_gate(
		false, &running, NULL), NULL);
	zassert_ok(ef800_firmware_application_update_gate(
		true, &running, &matching), NULL);
	zassert_equal(ef800_firmware_application_update_gate(
		true, &running, &different_patch), -ESTALE, NULL);
	zassert_equal(ef800_firmware_application_update_gate(
		true, NULL, &matching), -EINVAL, NULL);
}

ZTEST_SUITE(ef800_firmware_package, NULL, NULL, NULL, NULL, NULL);
