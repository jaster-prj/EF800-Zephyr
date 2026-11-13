/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Validate keyboard packages and run bounded updater transactions over I2C.
 */
#ifndef EF800_KEYBOARD_UPDATE_SERVICE_H
#define EF800_KEYBOARD_UPDATE_SERVICE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/fs/fs.h>

#include "ef800_keyboard_package.h"

struct ef800_keyboard_update_plan {
	uint32_t image_base;
	size_t image_limit;
	uint16_t target;
	uint16_t service_address;
};

struct ef800_keyboard_update_transaction {
	const struct device *i2c;
	const char *package_path;
	/* Set true during the transaction and cleared on every exit path. */
	bool *active;
	/* Opaque argument passed to every platform callback below. */
	void *context;
	/* Return true when the package storage is mounted / USB MSC is requested. */
	bool (*storage_mounted)(void *context);
	bool (*usb_msd_requested)(void *context);
	/* Enter resident/fallback updater or wait for service/application readiness. */
	int (*enter_resident_updater)(void *context);
	int (*wait_application_service)(void *context);
	int (*identify)(void *context, uint16_t address);
	int (*enter_updater_fallback)(void *context, uint8_t *version);
	/* Wait for the application image described by header to become available. */
	int (*wait_application)(void *context,
		const struct ef800_keyboard_package_header *header);
};

/** @brief Map package image kind to its authorized partition and service address.
 * @param image_kind Application, bootloader, or updater package kind.
 * @param plan Receives the selected base, limit, target, and I2C address.
 * @return 0 on a recognized kind; -EINVAL for invalid arguments or kind.
 */
int ef800_keyboard_update_plan_select(uint8_t image_kind,
	struct ef800_keyboard_update_plan *plan);

/** @brief Validate, transfer, verify, and finalize the package at package_path.
 * @param transaction I2C, package, active-state, ownership, and platform callbacks.
 * @return 0 if no package exists or the update completes; negative errno when
 *         blocked, invalid, or any file/protocol/update stage fails.
 */
int ef800_keyboard_update_transaction_run(
	const struct ef800_keyboard_update_transaction *transaction);

/** @brief Validate a package header and CRC of its complete image payload.
 * @param file Open package file positioned arbitrarily; it is repositioned.
 * @param header Receives the validated package header.
 * @param file_size Total package file size in bytes.
 * @return 0 when valid; negative errno for malformed data, I/O, or CRC failure.
 */
int ef800_keyboard_update_read_package(
	struct fs_file_t *file, struct ef800_keyboard_package_header *header,
	size_t file_size);

/** @brief Send a framed updater command and poll for its bounded response.
 * @param i2c Ready I2C controller device.
 * @param address 7-bit updater service address.
 * @param command Updater command byte.
 * @param payload Optional command payload.
 * @param payload_length Payload size in bytes.
 * @param response Buffer receiving the complete framed response.
 * @param response_length Expected response size.
 * @param timeout_ms Maximum time to poll the target.
 * @return 0 for a valid response; negative errno on invalid data, NACK, I/O, or timeout.
 */
int ef800_keyboard_update_request(const struct device *i2c, uint16_t address,
	uint8_t command, const uint8_t *payload, size_t payload_length,
	uint8_t *response, size_t response_length, uint32_t timeout_ms);

/** @brief Query and validate the updater's fixed-size version response.
 * @param i2c Ready I2C controller device.
 * @param address 7-bit updater service address.
 * @param version Buffer receiving the version frame.
 * @param timeout_ms Maximum time for command and response retries.
 * @return 0 on a valid response; negative errno on I/O, malformed response, or timeout.
 */
int ef800_keyboard_update_query_version(const struct device *i2c,
	uint16_t address, uint8_t *version, uint32_t timeout_ms);

/** @brief Decode an unaligned little-endian 32-bit value.
 * @param data Pointer to at least four bytes.
 * @return Decoded unsigned value.
 */
uint32_t ef800_keyboard_update_get_le32(const uint8_t *data);

/** @brief Encode a 16-bit value in little-endian byte order.
 * @param data Destination buffer of at least two bytes.
 * @param value Value to encode.
 * @return None.
 */
void ef800_keyboard_update_put_le16(uint8_t *data, uint16_t value);

/** @brief Encode a 32-bit value in little-endian byte order.
 * @param data Destination buffer of at least four bytes.
 * @param value Value to encode.
 * @return None.
 */
void ef800_keyboard_update_put_le32(uint8_t *data, uint32_t value);

#endif /* EF800_KEYBOARD_UPDATE_SERVICE_H */
