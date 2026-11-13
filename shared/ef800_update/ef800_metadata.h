/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Read and persist boot recovery metadata in flash/NVS. */
#ifndef EF800_METADATA_H
#define EF800_METADATA_H

#include <stdbool.h>

#include "ef800_update.h"

struct flash_area;

#define EF800_METADATA_NVS_ID 1U

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

#ifdef EF800_TEST_METADATA
/* Reset only the adapter's in-memory NVS cache between isolated test cases. */
void ef800_metadata_test_forget_state(void);
#endif

#endif /* EF800_METADATA_H */
