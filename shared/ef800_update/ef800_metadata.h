/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Read and persist boot recovery metadata in flash/NVS. */
#ifndef EF800_METADATA_H
#define EF800_METADATA_H

#include <stdbool.h>

#include "ef800_update.h"
#include "ef800_firmware_journal.h"

struct flash_area;

#define EF800_METADATA_NVS_ID 1U
#define EF800_FIRMWARE_JOURNAL_NVS_ID 2U

/** @brief Read, migrate if needed, and validate metadata in a flash area.
 * @param area Flash area containing metadata.
 * @param metadata Receives decoded metadata.
 * @param valid Receives whether usable metadata was found.
 * @return 0 on read; negative errno on storage or decode failure.
 */
int ef800_metadata_read(const struct flash_area *area,
				struct ef800_boot_metadata *metadata, bool *valid);

/** @brief Persist metadata to a specified flash area.
 * @param area Destination flash area.
 * @param metadata Metadata to encode and write.
 * @return 0 on write; negative errno on validation or flash failure.
 */
int ef800_metadata_write(const struct flash_area *area,
				struct ef800_boot_metadata *metadata);

/** @brief Read metadata through the configured current-platform adapter.
 * @param metadata Receives metadata.
 * @param valid Receives whether a valid record exists.
 * @return 0 on read; negative errno on storage/decode failure.
 */
int ef800_metadata_read_current(struct ef800_boot_metadata *metadata,
					bool *valid);

/** @brief Persist metadata through the configured current-platform adapter.
 * @param metadata Metadata to store.
 * @return 0 on write; negative errno on storage/encode failure.
 */
int ef800_metadata_write_current(struct ef800_boot_metadata *metadata);

/**
 * @brief Read and validate the transaction journal from boot-state NVS.
 *
 * @param area Flash area backing the boot-state NVS partition.
 * @param journal Non-NULL output receiving the latest valid journal.
 * @return 0 on success; -EINVAL for invalid arguments; -ENOENT if no journal
 *         has been written; or a negative errno for NVS/format/CRC failures.
 */
int ef800_firmware_journal_read(const struct flash_area *area,
				struct ef800_firmware_journal *journal);

/**
 * @brief Persist a journal to its independent boot-state NVS record.
 *
 * @param area Flash area backing the boot-state NVS partition.
 * @param journal Journal to persist; on success its sequence is updated to the
 *        sequence written. The NVS record is read back and compared before
 *        returning success.
 * @return 0 on verified persistence; -EINVAL for invalid arguments/records;
 *         or a negative errno for NVS, encoding, or readback failures.
 */
int ef800_firmware_journal_write(const struct flash_area *area,
				 struct ef800_firmware_journal *journal);

/**
 * @brief Read the journal from the platform's configured boot-state partition.
 *
 * @param journal Non-NULL output receiving the latest valid journal.
 * @return 0 on success; -ENOENT if not initialized; or a negative errno on
 *         flash-area, NVS, format, or CRC failure.
 */
int ef800_firmware_journal_read_current(
	struct ef800_firmware_journal *journal);

/**
 * @brief Persist the journal to the configured QSPI boot-state partition.
 *
 * @param journal Journal to persist; its sequence is updated on success.
 * @return 0 after verified persistence, or a negative errno on failure.
 */
int ef800_firmware_journal_write_current(
	struct ef800_firmware_journal *journal);

#ifdef EF800_TEST_METADATA
/* Reset only the adapter's in-memory NVS cache between isolated test cases. */
void ef800_metadata_test_forget_state(void);
#endif

#endif /* EF800_METADATA_H */
