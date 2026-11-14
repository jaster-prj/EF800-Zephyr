/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Shared image validation, CRC, boot metadata, and stable port IDs.
 */

#ifndef EF800_UPDATE_H_
#define EF800_UPDATE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Optional header for /fwu/EF800_FW.bin. Headerless files are also accepted. */
#define EF800_UPDATE_MAGIC           0x55384645U
#define EF800_UPDATE_FORMAT_VERSION 1U

#define EF800_BOOT_METADATA_MAGIC   0x45463842U
#define EF800_BOOT_METADATA_VERSION 3U
#define EF800_BOOT_METADATA_LEGACY_VERSION 1U
#define EF800_BOOT_METADATA_V2_VERSION 2U
#define EF800_METADATA_RESET 1
#define EF800_METADATA_MIGRATED 2
#define EF800_BOOT_METADATA_V2_SIZE 80U

#define EF800_BOOT_TARGET_MAIN   0U
#define EF800_BOOT_TARGET_BACKUP 1U

#define EF800_CONSOLE_FALLBACK_PORT 3U
#define EF800_CONSOLE_NO_TARGET 0xffU

/* Stable device IDs shared by the three L496 images. */
#define EF800_PORT_BUILDER_NONE 0U
#define EF800_PORT_BUILDER_DEBUG 1U
#define EF800_PORT_BUILDER_ELTA3 2U
#define EF800_PORT_BUILDER_FTS3XX 3U
#define EF800_PORT_BUILDER_MSD 4U
#define EF800_PORT_BUILDER_BLUENRG 5U
#define EF800_PORT_BUILDER_MTP 6U

/* Stable active connection IDs persisted with port assignments. */
#define EF800_PORT_CONNECTION_NONE 0U
#define EF800_PORT_CONNECTION_SERIAL 1U
#define EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL 2U
#define EF800_PORT_CONNECTION_PARALLEL 3U
#define EF800_PORT_CONNECTION_USB_CDC 4U
#define EF800_PORT_CONNECTION_USB_MSD 5U
#define EF800_PORT_CONNECTION_I2C 6U
#define EF800_PORT_CONNECTION_SPI 7U
#define EF800_PORT_CONNECTION_USB_MTP 8U
#define EF800_PORT_CONNECTION_USB EF800_PORT_CONNECTION_USB_CDC

struct ef800_update_header {
	uint32_t magic;
	uint16_t format_version;
	uint16_t header_size;
	uint32_t image_size;
	uint32_t image_crc32;
	uint32_t load_address;
};

/* Only watchdog recovery state is kept in external NVS. */
struct ef800_boot_metadata {
	uint32_t magic;
	uint16_t version;
	uint16_t size;
	uint32_t main_failures;
	uint32_t last_target;
	uint32_t sequence;
	uint32_t crc32;
};

#define EF800_BOOT_METADATA_SIZE 24U

/** @brief Validate initial stack pointer and reset vector against memory bounds.
 * @param vector Initial stack pointer and reset vector words.
 * @param image_address Image base address.
 * @param image_size Image extent in bytes.
 * @param sram_address Start of valid SRAM.
 * @param sram_size Size of valid SRAM.
 * @return True when both vector entries are valid for the supplied regions.
 */
bool ef800_vector_table_valid(const uint32_t vector[2], uintptr_t image_address,
				      size_t image_size, uintptr_t sram_address,
				      size_t sram_size);

/** @brief Check update-header fields and image length/address against a partition.
 * @param header Parsed update header.
 * @param file_size Complete file size in bytes.
 * @param image_address Authorized image base address.
 * @param image_partition_size Available partition capacity.
 * @return True when the header describes an image contained by the partition.
 */
bool ef800_update_header_valid(const struct ef800_update_header *header,
				       size_t file_size, uintptr_t image_address,
				       size_t image_partition_size);

/** @brief Compute CRC-32 over a complete byte range.
 * @param data Input bytes.
 * @param length Number of bytes.
 * @return CRC-32 value.
 */
uint32_t ef800_crc32(const uint8_t *data, size_t length);

/** @brief Continue CRC-32 calculation from an existing accumulator.
 * @param crc Previous accumulator value.
 * @param data Next input bytes.
 * @param length Number of bytes.
 * @return Updated CRC-32 accumulator.
 */
uint32_t ef800_crc32_update(uint32_t crc, const uint8_t *data, size_t length);

/** @brief Encode boot metadata in its current stable wire/storage format.
 * @param metadata Metadata fields to serialize.
 * @param buffer Destination byte buffer.
 * @param capacity Buffer capacity.
 * @return Encoded byte count, or negative errno if invalid/too small.
 */
int ef800_boot_metadata_encode(const struct ef800_boot_metadata *metadata,
					       uint8_t *buffer, size_t capacity);

/** @brief Decode and validate current-format boot metadata.
 * @param buffer Encoded metadata bytes.
 * @param length Available byte count.
 * @param metadata Receives decoded fields.
 * @return 0 on valid data; negative errno on malformed input or checksum error.
 */
int ef800_boot_metadata_decode(const uint8_t *buffer, size_t length,
					       struct ef800_boot_metadata *metadata);

/** @brief Migrate legacy version-1 metadata into the current representation.
 * @param buffer Legacy encoded bytes.
 * @param length Available byte count.
 * @param metadata Receives migrated metadata.
 * @return 0 on successful migration; negative errno if data is invalid.
 */
int ef800_boot_metadata_migrate_v1(const uint8_t *buffer, size_t length,
						       struct ef800_boot_metadata *metadata);

/** @brief Migrate version-2 metadata into the current representation.
 * @param buffer Version-2 encoded bytes.
 * @param length Available byte count.
 * @param metadata Receives migrated metadata.
 * @return 0 on successful migration; negative errno if data is invalid.
 */
int ef800_boot_metadata_migrate_v2(const uint8_t *buffer, size_t length,
						       struct ef800_boot_metadata *metadata);

/** @brief Initialize metadata with current magic, version, and default counters.
 * @param metadata State to initialize.
 * @return None.
 */
void ef800_boot_metadata_init(struct ef800_boot_metadata *metadata);

/** @brief Validate metadata fields and stored checksum.
 * @param metadata Metadata to check.
 * @return True when structure and checksum are valid.
 */
bool ef800_boot_metadata_valid(const struct ef800_boot_metadata *metadata);

/** @brief Calculate the checksum for a metadata record.
 * @param metadata Record whose fields are checksummed.
 * @return CRC-32 checksum.
 */
uint32_t ef800_boot_metadata_crc(const struct ef800_boot_metadata *metadata);

/** @brief Check whether a builder ID is permitted on a physical port.
 * @param port_id Port identifier.
 * @param builder_id Stable builder identifier.
 * @return True when that builder may be assigned to the port.
 */
bool ef800_port_builder_allowed_on_port(uint8_t port_id, uint8_t builder_id);

/** @brief Check whether a connection type is supported by a builder.
 * @param builder_id Stable builder identifier.
 * @param connection Stable connection identifier.
 * @return True when the pair is supported.
 */
bool ef800_port_builder_connection_valid(uint8_t builder_id,
								 uint8_t connection);

/** @brief Validate a complete persisted port assignment tuple.
 * @param port_id Port identifier.
 * @param builder_id Stable builder identifier.
 * @param connection Stable connection identifier.
 * @return True when all IDs and their compatibility constraints are valid.
 */
bool ef800_port_assignment_record_valid(uint8_t port_id, uint8_t builder_id,
								uint8_t connection);
#endif /* EF800_UPDATE_H_ */
