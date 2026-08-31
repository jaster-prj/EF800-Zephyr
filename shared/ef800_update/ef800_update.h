/* SPDX-License-Identifier: Apache-2.0 */

#ifndef EF800_UPDATE_H_
#define EF800_UPDATE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Optional header for /fwu/EF800_FW.bin. Headerless files are also accepted. */
#define EF800_UPDATE_MAGIC           0x55384645U
#define EF800_UPDATE_FORMAT_VERSION 1U

#define EF800_BOOT_METADATA_MAGIC   0x45463842U
#define EF800_BOOT_METADATA_VERSION 1U

#define EF800_BOOT_TARGET_MAIN   0U
#define EF800_BOOT_TARGET_BACKUP 1U

struct ef800_update_header {
	uint32_t magic;
	uint16_t format_version;
	uint16_t header_size;
	uint32_t image_size;
	uint32_t image_crc32;
	uint32_t load_address;
};

/* Kept at 32 bytes so it can be written with STM32L4 flash write alignment. */
struct ef800_boot_metadata {
	uint32_t magic;
	uint16_t version;
	uint16_t size;
	uint32_t main_failures;
	uint32_t last_target;
	uint32_t sequence;
	uint32_t reserved0;
	uint32_t reserved1;
	uint32_t crc32;
};

bool ef800_vector_table_valid(const uint32_t vector[2], uintptr_t image_address,
				      size_t image_size, uintptr_t sram_address,
				      size_t sram_size);

bool ef800_update_header_valid(const struct ef800_update_header *header,
				       size_t file_size, uintptr_t image_address,
				       size_t image_partition_size);

uint32_t ef800_crc32(const uint8_t *data, size_t length);
uint32_t ef800_crc32_update(uint32_t crc, const uint8_t *data, size_t length);

void ef800_boot_metadata_init(struct ef800_boot_metadata *metadata);
bool ef800_boot_metadata_valid(const struct ef800_boot_metadata *metadata);
uint32_t ef800_boot_metadata_crc(const struct ef800_boot_metadata *metadata);

#endif /* EF800_UPDATE_H_ */
