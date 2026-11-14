/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file
 * @brief Bounded persistent firmware-update transaction journal.
 *
 * @details The encoded format is little-endian and CRC-32 protected. It stores
 * at most six target records and is independent of the boot-selection NVS
 * record.
 *
 * The wire header is 20 bytes: magic:u32, version:u16, record_size:u16,
 * sequence:u32, target_count:u16, reserved:u16, and CRC-32:u32 at offset 16.
 * Each 20-byte target entry stores target:u16, phase:u8, result:u8,
 * package_crc32:u32, image_crc32:u32, semantic version:3*u16, and
 * reserved:u16. CRC-32 covers the complete record with its CRC field zero.
 */
#ifndef EF800_FIRMWARE_JOURNAL_H_
#define EF800_FIRMWARE_JOURNAL_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "ef800_firmware_package.h"

#define EF800_FIRMWARE_JOURNAL_MAGIC 0x314A4645U /* "EFJ1" */
#define EF800_FIRMWARE_JOURNAL_FORMAT_VERSION 1U
#define EF800_FIRMWARE_JOURNAL_MAX_TARGETS 6U
#define EF800_FIRMWARE_JOURNAL_HEADER_SIZE 20U
#define EF800_FIRMWARE_JOURNAL_TARGET_SIZE 20U
#define EF800_FIRMWARE_JOURNAL_CRC_OFFSET 16U
#define EF800_FIRMWARE_JOURNAL_MAX_SIZE \
	(EF800_FIRMWARE_JOURNAL_HEADER_SIZE + \
	 EF800_FIRMWARE_JOURNAL_MAX_TARGETS * \
	 EF800_FIRMWARE_JOURNAL_TARGET_SIZE)

enum ef800_firmware_journal_phase {
	EF800_FIRMWARE_JOURNAL_PENDING = 0U,
	EF800_FIRMWARE_JOURNAL_VALIDATED = 1U,
	EF800_FIRMWARE_JOURNAL_ERASING = 2U,
	EF800_FIRMWARE_JOURNAL_PROGRAMMING = 3U,
	EF800_FIRMWARE_JOURNAL_VERIFYING = 4U,
	EF800_FIRMWARE_JOURNAL_COMPLETE = 5U,
	EF800_FIRMWARE_JOURNAL_REJECTED = 6U,
	EF800_FIRMWARE_JOURNAL_RETRY_REQUIRED = 7U,
	EF800_FIRMWARE_JOURNAL_SKIPPED = 8U,
	EF800_FIRMWARE_JOURNAL_UNSUPPORTED = 9U,
};

enum ef800_firmware_journal_result {
	EF800_FIRMWARE_JOURNAL_RESULT_NONE = 0U,
	EF800_FIRMWARE_JOURNAL_RESULT_SUCCESS = 1U,
	EF800_FIRMWARE_JOURNAL_RESULT_REJECTED = 2U,
	EF800_FIRMWARE_JOURNAL_RESULT_RETRY = 3U,
	EF800_FIRMWARE_JOURNAL_RESULT_SKIPPED = 4U,
	EF800_FIRMWARE_JOURNAL_RESULT_UNSUPPORTED = 5U,
};

struct ef800_firmware_journal_target {
	/** Stable target ID from @ref ef800_firmware_target_id. */
	enum ef800_firmware_target_id target_id;
	/** Last durably recorded stage for this target. */
	enum ef800_firmware_journal_phase phase;
	/** Result associated with @ref phase; NONE while still in progress. */
	enum ef800_firmware_journal_result result;
	/** CRC-32 of the complete staged package used for this target. */
	uint32_t package_crc32;
	/** CRC-32 of the image payload declared by that package. */
	uint32_t image_crc32;
	/** Semantic version of the image declared by that package. */
	struct ef800_firmware_semver image_version;
};

struct ef800_firmware_journal {
	/** Monotonically incremented by the persistent write adapter. */
	uint32_t sequence;
	/** Number of valid entries in @ref targets. */
	uint16_t target_count;
	/** Unique target records; entries at/after target_count are ignored. */
	struct ef800_firmware_journal_target
		targets[EF800_FIRMWARE_JOURNAL_MAX_TARGETS];
};

/**
 * @brief Serialize and validate a journal record.
 *
 * @details Encodes only @p target_count entries. The destination record is
 * little-endian and includes a CRC-32 over the entire record with the CRC
 * field set to zero during calculation.
 *
 * @param journal Journal input; its sequence and records are not modified.
 * @param buffer Output buffer receiving the encoded record.
 * @param capacity Available bytes in @p buffer.
 * @return Encoded byte count on success; -EINVAL for invalid input/records;
 *         -EEXIST for duplicate targets; or -ENOSPC if the output is too small.
 */
int ef800_firmware_journal_encode(
	const struct ef800_firmware_journal *journal,
	uint8_t *buffer, size_t capacity);

/**
 * @brief Validate and decode a serialized journal record.
 *
 * @details The output is assigned only after the complete record, CRC,
 * reserved fields, target IDs, phases, results, and target uniqueness pass
 * validation.
 *
 * @param buffer Input record bytes; not modified.
 * @param length Exact encoded record length.
 * @param journal Non-NULL output receiving the decoded record on success.
 * @return 0 on success; -EINVAL for invalid arguments/records; -EPROTONOSUPPORT
 *         for an unsupported magic/version; -EMSGSIZE for inconsistent
 *         lengths; -EBADMSG for reserved-byte or CRC failure; or -EEXIST for
 *         duplicate targets.
 */
int ef800_firmware_journal_decode(
	const uint8_t *buffer, size_t length,
	struct ef800_firmware_journal *journal);

/**
 * @brief Convert interrupted mutations to explicit retry-required states.
 *
 * @param journal Journal to update in place; NULL or invalid journals are
 *        left unchanged.
 * @return true if an ERASING, PROGRAMMING, or VERIFYING entry was changed to
 *         RETRY_REQUIRED/RETRY. PENDING and VALIDATED entries remain resumable;
 *         the coordinator must read back a target before retrying an
 *         interrupted mutation. The caller must persist the modified journal
 *         with the journal write API before relying on the recovered state
 *         across another reset.
 */
bool ef800_firmware_journal_recover(struct ef800_firmware_journal *journal);

/**
 * @brief Check whether a target's staged package may be removed.
 *
 * @param target Target record to inspect; may be NULL.
 * @return true only for COMPLETE with SUCCESS result; false otherwise.
 */
bool ef800_firmware_journal_cleanup_allowed(
	const struct ef800_firmware_journal_target *target);

#endif /* EF800_FIRMWARE_JOURNAL_H_ */
