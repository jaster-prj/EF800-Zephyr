/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Detect legacy FAT layout on QSPI without modifying media. */
#ifndef EF800_QSPI_LAYOUT_H_
#define EF800_QSPI_LAYOUT_H_

#include <stdbool.h>
#include <stdint.h>

struct device;
struct flash_area;

#define EF800_QSPI_SECTOR_SIZE 512U

/* Identify the legacy single-volume FAT boot sector without modifying media. */

/** @brief Identify the known legacy FAT boot-sector layout.
 * @param sector First 512 bytes read from QSPI.
 * @return True when the sector matches the supported legacy layout.
 */
bool ef800_qspi_layout_is_legacy_fat(const uint8_t sector[EF800_QSPI_SECTOR_SIZE]);

/* Read QSPI offset zero and report whether it contains the legacy FAT volume. */

/** @brief Read QSPI offset zero and classify its legacy FAT layout.
 * @param flash QSPI flash device.
 * @param legacy Receives the classification result.
 * @return 0 on read; negative errno on invalid device or flash error.
 */
int ef800_qspi_layout_check_legacy(const struct device *flash, bool *legacy);

/* Check QSPI logical offset zero through its base flash-area mapping. */

/** @brief Classify offset zero through a base flash-area mapping.
 * @param base_area Base flash area for the QSPI device.
 * @param legacy Receives the classification result.
 * @return 0 on read; negative errno on invalid area or flash error.
 */
int ef800_qspi_layout_check_legacy_area(const struct flash_area *base_area,
						bool *legacy);

#endif /* EF800_QSPI_LAYOUT_H_ */
