/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_firmware_package.h"

#include <errno.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "ef800_update.h"

struct target_bounds {
	enum ef800_firmware_target_id target;
	enum ef800_firmware_coordinator_id coordinator;
	uint16_t hardware_id;
	uint32_t load_address;
	uint32_t image_limit;
};

static const struct target_bounds target_bounds[] = {
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

bool ef800_firmware_semver_equal(const struct ef800_firmware_semver *left,
				 const struct ef800_firmware_semver *right)
{
	return left != NULL && right != NULL &&
		left->major == right->major &&
		left->minor == right->minor &&
		left->patch == right->patch;
}

int ef800_firmware_application_update_gate(
	bool package_present, const struct ef800_firmware_semver *running_version,
	const struct ef800_firmware_semver *package_version)
{
	if (!package_present) {
		return 0;
	}
	if (running_version == NULL || package_version == NULL) {
		return -EINVAL;
	}
	return ef800_firmware_semver_equal(running_version, package_version) ?
		0 : -ESTALE;
}

static const struct target_bounds *find_target(enum ef800_firmware_target_id id)
{
	for (size_t index = 0U;
	     index < ARRAY_SIZE(target_bounds); index++) {
		if (target_bounds[index].target == id) {
			return &target_bounds[index];
		}
	}
	return NULL;
}

static int compare_version(const struct ef800_firmware_semver *left,
			   const struct ef800_firmware_semver *right)
{
	if (left->major != right->major) {
		return left->major > right->major ? 1 : -1;
	}
	if (left->minor != right->minor) {
		return left->minor > right->minor ? 1 : -1;
	}
	if (left->patch != right->patch) {
		return left->patch > right->patch ? 1 : -1;
	}
	return 0;
}

int ef800_firmware_package_header_parse(
	const uint8_t *package_header, size_t package_header_size,
	size_t package_size,
	enum ef800_firmware_target_id expected_target, uint16_t hardware_id,
	const struct ef800_firmware_semver *coordinator_version,
	struct ef800_firmware_package_header *header)
{
	uint8_t checked_header[EF800_FIRMWARE_PACKAGE_HEADER_SIZE];
	struct ef800_firmware_package_header parsed;
	const struct target_bounds *bounds;
	uint32_t image_size;

	if (package_header == NULL || coordinator_version == NULL || header == NULL ||
	    package_header_size != EF800_FIRMWARE_PACKAGE_HEADER_SIZE ||
	    package_size < EF800_FIRMWARE_PACKAGE_HEADER_SIZE) {
		return -EINVAL;
	}
	bounds = find_target(expected_target);
	if (bounds == NULL) {
		return -EINVAL;
	}
	if (sys_get_le32(&package_header[0]) != EF800_FIRMWARE_PACKAGE_MAGIC ||
	    sys_get_le16(&package_header[4]) != EF800_FIRMWARE_PACKAGE_FORMAT_VERSION ||
	    sys_get_le16(&package_header[6]) != EF800_FIRMWARE_PACKAGE_HEADER_SIZE) {
		return -EPROTONOSUPPORT;
	}
	if (sys_get_le16(&package_header[8]) != expected_target) {
		return -EXDEV;
	}
	if (sys_get_le16(&package_header[10]) != bounds->coordinator ||
	    sys_get_le16(&package_header[12]) != hardware_id ||
	    hardware_id != bounds->hardware_id ||
	    sys_get_le32(&package_header[26]) != bounds->load_address) {
		return -EXDEV;
	}

	image_size = sys_get_le32(&package_header[30]);
	if (image_size == 0U || image_size > bounds->image_limit ||
	    package_size != (size_t)EF800_FIRMWARE_PACKAGE_HEADER_SIZE +
			    (size_t)image_size) {
		return -EMSGSIZE;
	}
	for (size_t index = EF800_FIRMWARE_PACKAGE_RESERVED_OFFSET;
	     index < EF800_FIRMWARE_PACKAGE_HEADER_SIZE; index++) {
		if (package_header[index] != 0U) {
			return -EBADMSG;
		}
	}

	memcpy(checked_header, package_header, sizeof(checked_header));
	memset(&checked_header[EF800_FIRMWARE_PACKAGE_HEADER_CRC_OFFSET], 0,
	       sizeof(uint32_t));
	if (ef800_crc32(checked_header, sizeof(checked_header)) !=
	    sys_get_le32(&package_header[EF800_FIRMWARE_PACKAGE_HEADER_CRC_OFFSET])) {
		return -EBADMSG;
	}

	parsed.format_version = sys_get_le16(&package_header[4]);
	parsed.header_size = sys_get_le16(&package_header[6]);
	parsed.target_id =
		(enum ef800_firmware_target_id)sys_get_le16(&package_header[8]);
	parsed.coordinator_id =
		(enum ef800_firmware_coordinator_id)sys_get_le16(&package_header[10]);
	parsed.hardware_id = sys_get_le16(&package_header[12]);
	parsed.image_version.major = sys_get_le16(&package_header[14]);
	parsed.image_version.minor = sys_get_le16(&package_header[16]);
	parsed.image_version.patch = sys_get_le16(&package_header[18]);
	parsed.minimum_coordinator_version.major =
		sys_get_le16(&package_header[20]);
	parsed.minimum_coordinator_version.minor =
		sys_get_le16(&package_header[22]);
	parsed.minimum_coordinator_version.patch =
		sys_get_le16(&package_header[24]);
	parsed.load_address = sys_get_le32(&package_header[26]);
	parsed.image_size = image_size;
	parsed.image_crc32 = sys_get_le32(&package_header[34]);
	parsed.header_crc32 =
		sys_get_le32(
			&package_header[EF800_FIRMWARE_PACKAGE_HEADER_CRC_OFFSET]);
	if (compare_version(coordinator_version,
			    &parsed.minimum_coordinator_version) < 0) {
		return -EPROTONOSUPPORT;
	}

	*header = parsed;
	return 0;
}

int ef800_firmware_package_parse(
	const uint8_t *package, size_t package_size,
	enum ef800_firmware_target_id expected_target, uint16_t hardware_id,
	const struct ef800_firmware_semver *coordinator_version,
	struct ef800_firmware_package_header *header, const uint8_t **image)
{
	int rc;

	if (package == NULL || image == NULL) {
		return -EINVAL;
	}
	rc = ef800_firmware_package_header_parse(
		package, EF800_FIRMWARE_PACKAGE_HEADER_SIZE, package_size,
		expected_target, hardware_id, coordinator_version, header);
	if (rc != 0) {
		return rc;
	}
	if (ef800_crc32(&package[EF800_FIRMWARE_PACKAGE_HEADER_SIZE],
			header->image_size) != header->image_crc32) {
		return -EBADMSG;
	}
	*image = &package[EF800_FIRMWARE_PACKAGE_HEADER_SIZE];
	return 0;
}

int ef800_firmware_package_set_validate(const uint16_t *target_ids, size_t count)
{
	uint8_t seen = 0U;

	if (target_ids == NULL && count != 0U) {
		return -EINVAL;
	}
	for (size_t index = 0U; index < count; index++) {
		uint16_t target_id = target_ids[index];

		if (target_id < EF800_FIRMWARE_TARGET_BOOTLOADER ||
		    target_id > EF800_FIRMWARE_TARGET_KEYBOARD_APPLICATION) {
			return -EINVAL;
		}
		uint8_t bit = (uint8_t)(1U << (target_id - 1U));
		if ((seen & bit) != 0U) {
			return -EEXIST;
		}
		seen |= bit;
	}
	return 0;
}
