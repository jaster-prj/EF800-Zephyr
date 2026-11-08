/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_KEYBOARD_PACKAGE_H_
#define EF800_KEYBOARD_PACKAGE_H_

#include <stdbool.h>
#include <stdint.h>

#define EF800_KEYBOARD_PACKAGE_MAGIC 0x4B504645U /* "EFPK" */
#define EF800_KEYBOARD_PACKAGE_FORMAT 2U
#define EF800_KEYBOARD_PACKAGE_TARGET 0x0435U
#define EF800_KEYBOARD_PACKAGE_BASE 0x08008000U
#define EF800_KEYBOARD_PACKAGE_LIMIT 0x00030000U
#define EF800_KEYBOARD_BOOT_BASE 0x08000000U
#define EF800_KEYBOARD_BOOT_LIMIT 0x00008000U
#define EF800_KEYBOARD_UPDATER_BASE 0x08038000U
#define EF800_KEYBOARD_UPDATER_LIMIT 0x00008000U
#define EF800_KEYBOARD_IMAGE_APPLICATION 1U
#define EF800_KEYBOARD_IMAGE_BOOTLOADER 2U
#define EF800_KEYBOARD_IMAGE_UPDATER 3U
#define EF800_KEYBOARD_PACKAGE_FLAG_FORCE 0x00000001U
#define EF800_KEYBOARD_PACKAGE_HEADER_SIZE 32U

struct ef800_keyboard_package_header {
	uint32_t magic;
	uint16_t format;
	uint16_t header_size;
	uint32_t target_id;
	uint32_t image_base;
	uint32_t image_size;
	uint32_t image_crc32;
	uint8_t version_major;
	uint8_t version_minor;
	uint8_t version_patch;
	uint8_t image_kind;
	uint32_t flags;
};

bool ef800_keyboard_package_valid(
	const struct ef800_keyboard_package_header *header,
	size_t file_size);

#endif /* EF800_KEYBOARD_PACKAGE_H_ */
