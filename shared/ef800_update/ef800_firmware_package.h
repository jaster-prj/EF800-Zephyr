/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Wire contract for staged, target-bound firmware packages.
 */

#ifndef EF800_FIRMWARE_PACKAGE_H_
#define EF800_FIRMWARE_PACKAGE_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * All multibyte wire fields are little-endian. The 64-byte header is encoded
 * field-by-field; it is not a serialized C structure.
 *
 * Offsets:
 *   0  magic:u32 ("EFW1")
 *   4  format_version:u16
 *   6  header_size:u16
 *   8  target_id:u16
 *   10 coordinator_id:u16
 *   12 hardware_id:u16 (STM32 device ID)
 *   14 image_version:{major:u16, minor:u16, patch:u16}
 *   20 minimum_coordinator_version:{major:u16, minor:u16, patch:u16}
 *   26 load_address:u32
 *   30 image_size:u32
 *   34 image_crc32:u32 (CRC-32 of the image bytes only)
 *   38 header_crc32:u32 (CRC-32 of all 64 header bytes with this field zero)
 *   42 reserved[22]: all zero in format version 1
 *
 * The package is exactly header_size + image_size bytes. Both CRC fields use
 * the existing CRC-32/ISO-HDLC implementation. CRC detects accidental
 * corruption; it does not authenticate the publisher.
 */
#define EF800_FIRMWARE_PACKAGE_MAGIC                 0x31574645U
#define EF800_FIRMWARE_PACKAGE_FORMAT_VERSION        1U
#define EF800_FIRMWARE_PACKAGE_HEADER_SIZE           64U
#define EF800_FIRMWARE_PACKAGE_HEADER_CRC_OFFSET     38U
#define EF800_FIRMWARE_PACKAGE_RESERVED_OFFSET       42U
#define EF800_FIRMWARE_PACKAGE_RESERVED_SIZE         22U

#define EF800_FIRMWARE_PACKAGE_FILE_BOOTLOADER       "EF800_BOOT.pkg"
#define EF800_FIRMWARE_PACKAGE_FILE_BACKUP           "EF800_BACKUP.pkg"
#define EF800_FIRMWARE_PACKAGE_FILE_APPLICATION      "EF800_APP.pkg"
#define EF800_FIRMWARE_PACKAGE_FILE_KEYBOARD_UPDATER "EF800_KEYBOARD_UPDATER.pkg"
#define EF800_FIRMWARE_PACKAGE_FILE_KEYBOARD_BOOT    "EF800_KEYBOARD_BOOT.pkg"
#define EF800_FIRMWARE_PACKAGE_FILE_KEYBOARD_APP     "EF800_KEYBOARD_APP.pkg"

/* The bootstrap is intentionally not an envelope package. */
#define EF800_FIRMWARE_PACKAGE_LEGACY_BOOTSTRAP_FILE "EF800_FW.bin"

/*
 * Stable IDs identify both the package target and its authorized image region:
 *   1 EF800 Bootloader      L496 0x08000000 / 0x10000; EF800 Application
 *   2 EF800 Backup          L496 0x08010000 / 0x20000; EF800 Application
 *   3 EF800 Application     L496 0x08030000 / 0xD0000; EF800 Bootloader
 *   4 Keyboard Updater      L431 0x08038000 / 0x08000; EF800 Application
 *   5 Keyboard Bootloader   L431 0x08000000 / 0x08000; EF800 Application
 *   6 Keyboard Application  L431 0x08008000 / 0x30000; EF800 Application
 */
enum ef800_firmware_target_id {
	EF800_FIRMWARE_TARGET_BOOTLOADER = 1U,
	EF800_FIRMWARE_TARGET_BACKUP = 2U,
	EF800_FIRMWARE_TARGET_APPLICATION = 3U,
	EF800_FIRMWARE_TARGET_KEYBOARD_UPDATER = 4U,
	EF800_FIRMWARE_TARGET_KEYBOARD_BOOTLOADER = 5U,
	EF800_FIRMWARE_TARGET_KEYBOARD_APPLICATION = 6U,
};

/** Firmware component whose version is checked against a package minimum. */
enum ef800_firmware_coordinator_id {
	EF800_FIRMWARE_COORDINATOR_BOOTLOADER = 1U,
	EF800_FIRMWARE_COORDINATOR_APPLICATION = 2U,
};

#define EF800_FIRMWARE_HARDWARE_ID_L496 0x0461U
#define EF800_FIRMWARE_HARDWARE_ID_L431 0x0435U

#define EF800_FIRMWARE_L496_BOOTLOADER_ADDRESS 0x08000000U
#define EF800_FIRMWARE_L496_BOOTLOADER_SIZE    0x00010000U
#define EF800_FIRMWARE_L496_BACKUP_ADDRESS     0x08010000U
#define EF800_FIRMWARE_L496_BACKUP_SIZE        0x00020000U
#define EF800_FIRMWARE_L496_APPLICATION_ADDRESS 0x08030000U
#define EF800_FIRMWARE_L496_APPLICATION_SIZE   0x000D0000U

#define EF800_FIRMWARE_L431_BOOTLOADER_ADDRESS 0x08000000U
#define EF800_FIRMWARE_L431_BOOTLOADER_SIZE    0x00008000U
#define EF800_FIRMWARE_L431_APPLICATION_ADDRESS 0x08008000U
#define EF800_FIRMWARE_L431_APPLICATION_SIZE   0x00030000U
#define EF800_FIRMWARE_L431_UPDATER_ADDRESS    0x08038000U
#define EF800_FIRMWARE_L431_UPDATER_SIZE       0x00008000U

#define EF800_FIRMWARE_PACKAGE_MAX_IMAGE_SIZE \
	EF800_FIRMWARE_L496_APPLICATION_SIZE
#define EF800_FIRMWARE_PACKAGE_MAX_FILE_SIZE \
	(EF800_FIRMWARE_PACKAGE_HEADER_SIZE + \
	 EF800_FIRMWARE_PACKAGE_MAX_IMAGE_SIZE)

#define EF800_FIRMWARE_INITIAL_VERSION_MAJOR 1U
#define EF800_FIRMWARE_INITIAL_VERSION_MINOR 0U
#define EF800_FIRMWARE_INITIAL_VERSION_PATCH 0U
#define EF800_FIRMWARE_INITIAL_MIN_COORDINATOR_VERSION_MAJOR 1U
#define EF800_FIRMWARE_INITIAL_MIN_COORDINATOR_VERSION_MINOR 0U
#define EF800_FIRMWARE_INITIAL_MIN_COORDINATOR_VERSION_PATCH 0U

/** Three-component semantic version; each wire component is an unsigned u16. */
struct ef800_firmware_semver {
	/** Major version component. */
	uint16_t major;
	/** Minor version component. */
	uint16_t minor;
	/** Patch version component. */
	uint16_t patch;
};

/** Compare all three semantic-version components for exact equality. */
bool ef800_firmware_semver_equal(const struct ef800_firmware_semver *left,
				 const struct ef800_firmware_semver *right);

/** Return -ESTALE only when a staged APP package version differs from runtime. */
int ef800_firmware_application_update_gate(
	bool package_present, const struct ef800_firmware_semver *running_version,
	const struct ef800_firmware_semver *package_version);

/**
 * @brief Host-endian representation of a validated package header.
 *
 * @details This structure is an API result, not the serialized wire layout.
 */
struct ef800_firmware_package_header {
	uint16_t format_version;
	uint16_t header_size;
	enum ef800_firmware_target_id target_id;
	enum ef800_firmware_coordinator_id coordinator_id;
	uint16_t hardware_id;
	struct ef800_firmware_semver image_version;
	struct ef800_firmware_semver minimum_coordinator_version;
	uint32_t load_address;
	uint32_t image_size;
	uint32_t image_crc32;
	uint32_t header_crc32;
};

/**
 * @brief Parse and validate a package header without buffering its payload.
 *
 * @param wire_header Pointer to exactly one wire header; not modified.
 * @param wire_header_size Number of available header bytes; must be 64.
 * @param package_size Total package length, including header and payload.
 * @param expected_target Target authorized by the selected staged filename.
 * @param hardware_id Detected STM32 device ID for the MCU being updated.
 * @param coordinator_version Version of the running authorized coordinator.
 * @param header Non-NULL output receiving decoded, validated header fields.
 * @return 0 when the header and declared package length are valid; -EINVAL
 *         for invalid arguments/target; -EPROTONOSUPPORT for unsupported
 *         format or coordinator version; -EXDEV for target, hardware,
 *         coordinator-ID, or load-address mismatch; -EMSGSIZE for invalid
 *         image/file bounds; or -EBADMSG for invalid reserved bytes/header CRC.
 */
int ef800_firmware_package_header_parse(
	const uint8_t *wire_header, size_t wire_header_size, size_t package_size,
	enum ef800_firmware_target_id expected_target, uint16_t hardware_id,
	const struct ef800_firmware_semver *coordinator_version,
	struct ef800_firmware_package_header *header);

/**
 * @brief Parse a complete in-memory package and validate both CRCs.
 *
 * @param package Pointer to the complete package in memory; not modified.
 * @param package_size Exact number of available bytes at @p package.
 * @param expected_target Target authorized by the selected staged filename.
 * @param hardware_id Detected STM32 device ID for the MCU being updated.
 * @param coordinator_version Version of the running authorized coordinator.
 * @param header Non-NULL output receiving decoded, validated header fields.
 * @param image Non-NULL output receiving a borrowed pointer into @p package.
 *        The payload remains valid only while @p package remains valid.
 * @return 0 when package and payload CRCs are valid; otherwise the same
 *         validation errors as @ref ef800_firmware_package_header_parse, or
 *         -EBADMSG for an invalid payload CRC.
 */
int ef800_firmware_package_parse(
	const uint8_t *package, size_t package_size,
	enum ef800_firmware_target_id expected_target, uint16_t hardware_id,
	const struct ef800_firmware_semver *coordinator_version,
	struct ef800_firmware_package_header *header, const uint8_t **image);

/**
 * @brief Reject unknown or repeated targets in a staged package set.
 *
 * @param target_ids Array of target IDs decoded from staged package headers.
 *        May be NULL only when @p count is zero.
 * @param count Number of entries in @p target_ids; zero represents an empty
 *        optional package set and is valid.
 * @return 0 if every ID is known and unique; -EINVAL for a NULL non-empty
 *         array or unknown target; -EEXIST if an ID occurs more than once.
 */
int ef800_firmware_package_set_validate(const uint16_t *target_ids,
				       size_t count);

#endif /* EF800_FIRMWARE_PACKAGE_H_ */
