/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_QSPI_LAYOUT_H_
#define EF800_QSPI_LAYOUT_H_

#include <stdbool.h>
#include <stdint.h>

struct device;
struct flash_area;

#define EF800_QSPI_SECTOR_SIZE 512U

/* Identify the legacy single-volume FAT boot sector without modifying media. */
bool ef800_qspi_layout_is_legacy_fat(const uint8_t sector[EF800_QSPI_SECTOR_SIZE]);

/* Read QSPI offset zero and report whether it contains the legacy FAT volume. */
int ef800_qspi_layout_check_legacy(const struct device *flash, bool *legacy);

/* Check QSPI logical offset zero through its base flash-area mapping. */
int ef800_qspi_layout_check_legacy_area(const struct flash_area *base_area,
						bool *legacy);

#endif /* EF800_QSPI_LAYOUT_H_ */
