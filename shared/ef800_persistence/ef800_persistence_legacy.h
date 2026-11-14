/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Decode and migrate deployed Mbed persistence records. */
#ifndef EF800_PERSISTENCE_LEGACY_H
#define EF800_PERSISTENCE_LEGACY_H

#include <stddef.h>
#include <stdint.h>

#include "ef800_measurement.h"
#include "ef800_persistence.h"
#include "ef800_time.h"

/* Files written by the deployed Mbed Persistence<T> implementation. */
#define EF800_LEGACY_TIME_FILE "0"
#define EF800_LEGACY_DEBUG_FILE "1"
#define EF800_LEGACY_ELTA3_FILE "10"
#define EF800_LEGACY_FTS3XX_FILE "11"
#define EF800_LEGACY_PORT_ASSIGNMENT_FILE_PREFIX "port_"

/* Explicit Zephyr records produced after a successful migration. */
#define EF800_TIME_SETTINGS_FILE "time_settings"
#define EF800_DEBUG_CONFIG_FILE_PREFIX "debug_"
#define EF800_ELTA3_CONFIG_FILE_PREFIX "elta3_"
#define EF800_FTS3XX_CONFIG_FILE_PREFIX "fts3xx_"

#define EF800_SERIAL_CONFIG_MAGIC 0x53434647U
#define EF800_SERIAL_CONFIG_VERSION 1U
#define EF800_SERIAL_CONFIG_RECORD_SIZE 24U

enum ef800_legacy_serial_kind {
	EF800_LEGACY_SERIAL_DEBUG = 0,
	EF800_LEGACY_SERIAL_ELTA3,
	EF800_LEGACY_SERIAL_FTS3XX,
};

/*
 * The deployed Mbed image used 32-bit little-endian int and enum values on
 * its Cortex-M target. These parsers describe those bytes explicitly; they do
 * not cast legacy input to a C or C++ structure.
 */

/** @brief Parse deployed little-endian time settings.
 * @param buffer Legacy record bytes.
 * @param length Record byte count.
 * @param settings Receives migrated settings.
 * @return 0 on parse; negative errno for invalid format/range.
 */
int ef800_legacy_parse_time(const uint8_t *buffer, size_t length,
				    struct ef800_time_settings *settings);

/** @brief Parse a deployed legacy builder assignment record.
 * @param buffer Legacy record bytes.
 * @param length Record byte count.
 * @param builder_id Receives the stable builder ID.
 * @return 0 on parse; negative errno for invalid size/ID.
 */
int ef800_legacy_parse_port_assignment(const uint8_t *buffer, size_t length,
					       uint8_t *builder_id);

/** @brief Parse deployed serial configuration bytes for one device/port.
 * @param buffer Legacy record bytes.
 * @param length Record byte count.
 * @param kind Device configuration kind.
 * @param port_id Port associated with the record.
 * @param config Receives validated serial settings.
 * @return 0 on parse; negative errno for invalid record or unsupported settings.
 */
int ef800_legacy_parse_serial_config(
	const uint8_t *buffer, size_t length,
	enum ef800_legacy_serial_kind kind, uint8_t port_id,
	struct ef800_measurement_serial_config *config);

/** @brief Encode current time settings into a printable persistent record.
 * @param settings Settings to serialize.
 * @param buffer Destination buffer.
 * @param capacity Buffer capacity.
 * @param length Receives encoded size.
 * @return 0 on encode; negative errno for invalid settings or short buffer.
 */
int ef800_time_settings_encode(const struct ef800_time_settings *settings,
				       char *buffer, size_t capacity, size_t *length);

/** @brief Decode a current-format time settings record.
 * @param buffer Encoded record bytes.
 * @param length Record byte count.
 * @param settings Receives decoded settings.
 * @return 0 on decode; negative errno for malformed data.
 */
int ef800_time_settings_decode(const uint8_t *buffer, size_t length,
				       struct ef800_time_settings *settings);

/** @brief Encode serial settings with stable versioned fields.
 * @param config Settings to serialize.
 * @param buffer Destination buffer.
 * @param capacity Buffer capacity.
 * @param length Receives encoded size.
 * @return 0 on encode; negative errno for invalid settings or short buffer.
 */
int ef800_serial_config_encode(
	const struct ef800_measurement_serial_config *config,
	uint8_t *buffer, size_t capacity, size_t *length);

/** @brief Decode and validate a current serial configuration record.
 * @param buffer Encoded record bytes.
 * @param length Record byte count.
 * @param config Receives serial settings.
 * @return 0 on decode; negative errno for malformed or unsupported values.
 */
int ef800_serial_config_decode(const uint8_t *buffer, size_t length,
				       struct ef800_measurement_serial_config *config);

/* Migrate one logical record without deleting an unverified legacy source. */

/** @brief Migrate the legacy time-settings record without deleting its source.
 * @param storage Persistence service.
 * @return 0 on migration/already-current; negative errno on validation or I/O failure.
 */
int ef800_legacy_migrate_time(struct ef800_persistence *storage);

/** @brief Migrate one legacy port-assignment record.
 * @param storage Persistence service.
 * @param port_id Port whose legacy record is migrated.
 * @return 0 on migration/already-current; negative errno on parse or I/O failure.
 */
int ef800_legacy_migrate_port_assignment(struct ef800_persistence *storage,
						 uint8_t port_id);

/** @brief Migrate one legacy per-device serial configuration.
 * @param storage Persistence service.
 * @param kind Serial configuration kind.
 * @param port_id Port identifier.
 * @return 0 on migration/already-current; negative errno on parse or I/O failure.
 */
int ef800_legacy_migrate_serial_config(
	struct ef800_persistence *storage, enum ef800_legacy_serial_kind kind,
	uint8_t port_id);

/** @brief Load current serial settings, migrating a legacy record if needed.
 * @param storage Persistence service.
 * @param kind Device configuration kind.
 * @param port_id Port identifier.
 * @param config Receives validated serial settings.
 * @return 0 on load; negative errno when missing, malformed, or unreadable.
 */
int ef800_serial_config_load(struct ef800_persistence *storage,
				     enum ef800_legacy_serial_kind kind, uint8_t port_id,
				     struct ef800_measurement_serial_config *config);

/* Migrate all supported records and remove legacy aggregate files only after
 * every record has been converted and verified. */

/** @brief Migrate all supported legacy records and verify before cleanup.
 * @param storage Persistence service.
 * @return 0 when migration completes; negative errno on any conversion/storage failure.
 */
int ef800_legacy_migrate_all(struct ef800_persistence *storage);

#endif /* EF800_PERSISTENCE_LEGACY_H */
