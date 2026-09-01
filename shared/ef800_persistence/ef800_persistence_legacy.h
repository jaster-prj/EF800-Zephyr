/* SPDX-License-Identifier: Apache-2.0 */
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
int ef800_legacy_parse_time(const uint8_t *buffer, size_t length,
				    struct ef800_time_settings *settings);
int ef800_legacy_parse_port_assignment(const uint8_t *buffer, size_t length,
					       uint8_t *builder_id);
int ef800_legacy_parse_serial_config(
	const uint8_t *buffer, size_t length,
	enum ef800_legacy_serial_kind kind, uint8_t port_id,
	struct ef800_measurement_serial_config *config);

int ef800_time_settings_encode(const struct ef800_time_settings *settings,
				       char *buffer, size_t capacity, size_t *length);
int ef800_time_settings_decode(const uint8_t *buffer, size_t length,
				       struct ef800_time_settings *settings);
int ef800_serial_config_encode(
	const struct ef800_measurement_serial_config *config,
	uint8_t *buffer, size_t capacity, size_t *length);
int ef800_serial_config_decode(const uint8_t *buffer, size_t length,
				       struct ef800_measurement_serial_config *config);

/* Migrate one logical record without deleting an unverified legacy source. */
int ef800_legacy_migrate_time(struct ef800_persistence *storage);
int ef800_legacy_migrate_port_assignment(struct ef800_persistence *storage,
						 uint8_t port_id);
int ef800_legacy_migrate_serial_config(
	struct ef800_persistence *storage, enum ef800_legacy_serial_kind kind,
	uint8_t port_id);
int ef800_serial_config_load(struct ef800_persistence *storage,
				     enum ef800_legacy_serial_kind kind, uint8_t port_id,
				     struct ef800_measurement_serial_config *config);

/* Migrate all supported records and remove legacy aggregate files only after
 * every record has been converted and verified. */
int ef800_legacy_migrate_all(struct ef800_persistence *storage);

#endif /* EF800_PERSISTENCE_LEGACY_H */
