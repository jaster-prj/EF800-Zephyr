/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_persistence_legacy.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "ef800_ports.h"
#include "ef800_update.h"

#define EF800_LEGACY_MAX_FILE_SIZE 512U
#define EF800_LEGACY_MAX_RECORDS 16U
#define EF800_LEGACY_TIME_RECORD_SIZE 12U
#define EF800_LEGACY_SERIAL_RECORD_SIZE 24U

static const char *legacy_serial_file(enum ef800_legacy_serial_kind kind);
static const char *serial_config_prefix(enum ef800_legacy_serial_kind kind);
static int read_file(struct ef800_persistence *storage, const char *name,
			     uint8_t *buffer, size_t capacity, size_t *length);

static bool serial_config_equal(
	const struct ef800_measurement_serial_config *left,
	const struct ef800_measurement_serial_config *right)
{
	return left != NULL && right != NULL && left->baudrate == right->baudrate &&
		left->data_bits == right->data_bits && left->parity == right->parity &&
		left->stop_bits == right->stop_bits && left->flow_control == right->flow_control;
}

static bool assignment_equal(const struct ef800_port_assignment_state *left,
				     const struct ef800_port_assignment_state *right)
{
	return left != NULL && right != NULL && left->port_id == right->port_id &&
		left->builder_id == right->builder_id && left->connection == right->connection &&
		left->assigned == right->assigned;
}

static int assignment_name(char *name, size_t capacity, const char *prefix,
				   uint8_t port_id)
{
	int result = snprintf(name, capacity, "%s%u", prefix, port_id);

	return result < 0 || (size_t)result >= capacity ? -ENAMETOOLONG : 0;
}

static int parse_legacy_assignment(const uint8_t *buffer, size_t length,
					   uint8_t port_id,
					   struct ef800_port_assignment_state *state)
{
	uint8_t builder_id;
	int result;

	result = ef800_legacy_parse_port_assignment(buffer, length, &builder_id);
	if (result != 0 || !ef800_port_builder_allowed_on_port(port_id, builder_id)) {
		return -EINVAL;
	}
	*state = (struct ef800_port_assignment_state){
		.port_id = port_id,
		.builder_id = builder_id,
		.connection = (uint8_t)ef800_port_assignment_default_connection(
			port_id, builder_id),
		.assigned = true,
	};
	return 0;
}

static int legacy_records(const uint8_t *buffer, size_t length,
				 size_t record_size, uint32_t *count)
{
	uint32_t raw_count;
	size_t payload_length;

	if (buffer == NULL || count == NULL || length < sizeof(uint32_t)) {
		return -EINVAL;
	}
	raw_count = sys_get_le32(buffer);
	if (raw_count > INT32_MAX || raw_count > EF800_LEGACY_MAX_RECORDS) {
		return -EINVAL;
	}
	payload_length = (size_t)raw_count * record_size;
	if (payload_length > SIZE_MAX - sizeof(uint32_t) ||
		length != sizeof(uint32_t) + payload_length) {
		return -EINVAL;
	}
	*count = raw_count;
	return 0;
}

static bool valid_time_settings(const struct ef800_time_settings *settings)
{
	return settings != NULL && settings->utc_offset_minutes >= -24 * 60 &&
		settings->utc_offset_minutes <= 24 * 60;
}

int ef800_legacy_parse_time(const uint8_t *buffer, size_t length,
				    struct ef800_time_settings *settings)
{
	uint32_t count;
	uint32_t seen = 0U;
	int result;

	if (settings == NULL) {
		return -EINVAL;
	}
	result = legacy_records(buffer, length, EF800_LEGACY_TIME_RECORD_SIZE,
					&count);
	if (result != 0) {
		return result;
	}
	for (uint32_t index = 0U; index < count; ++index) {
		size_t offset = sizeof(uint32_t) +
			(size_t)index * EF800_LEGACY_TIME_RECORD_SIZE;
		int32_t entry_id = (int32_t)sys_get_le32(&buffer[offset]);
		int32_t hours = (int32_t)sys_get_le32(&buffer[offset + 4U]);
		uint8_t daylight_saving = buffer[offset + 8U];

		if (entry_id != 0 || (seen & BIT(0)) != 0U ||
			hours < -24 || hours > 24 || daylight_saving > 1U) {
			return -EINVAL;
		}
		seen |= BIT(0);
		settings->utc_offset_minutes = hours * 60;
		settings->daylight_saving = daylight_saving != 0U;
	}
	return count == 1U ? 0 : -ENOENT;
}

static int parse_decimal(const uint8_t *buffer, size_t length, long *value)
{
	char text[16];
	char *end;

	if (buffer == NULL || value == NULL || length == 0U || length >= sizeof(text) ||
		memchr(buffer, '\0', length) != NULL) {
		return -EINVAL;
	}
	memcpy(text, buffer, length);
	text[length] = '\0';
	while (length > 0U && (text[length - 1U] == '\r' ||
				       text[length - 1U] == '\n')) {
		text[--length] = '\0';
	}
	if (length == 0U || text[0] < '0' || text[0] > '9') {
		return -EINVAL;
	}
	*value = strtol(text, &end, 10);
	return *end == '\0' && *value >= 1L && *value <= 4L ? 0 : -EINVAL;
}

int ef800_legacy_parse_port_assignment(const uint8_t *buffer, size_t length,
					       uint8_t *builder_id)
{
	long value;

	if (builder_id == NULL || parse_decimal(buffer, length, &value) != 0) {
		return -EINVAL;
	}
	*builder_id = (uint8_t)value;
	return 0;
}

static bool serial_kind_valid(enum ef800_legacy_serial_kind kind)
{
	return kind >= EF800_LEGACY_SERIAL_DEBUG && kind <= EF800_LEGACY_SERIAL_FTS3XX;
}

static bool serial_payload_valid(const uint8_t *payload)
{
	uint32_t baudrate = sys_get_le32(&payload[0]);
	uint32_t data_bits = sys_get_le32(&payload[4]);
	uint32_t parity = sys_get_le32(&payload[8]);
	uint32_t stop_bits = sys_get_le32(&payload[12]);
	uint32_t flow = sys_get_le32(&payload[16]);

	return baudrate > 0U && baudrate <= 10000000U &&
		data_bits >= 5U && data_bits <= 8U && parity <= 2U &&
		(stop_bits == 1U || stop_bits == 2U) && flow <= 3U;
}

int ef800_legacy_parse_serial_config(
	const uint8_t *buffer, size_t length, enum ef800_legacy_serial_kind kind,
	uint8_t port_id, struct ef800_measurement_serial_config *config)
{
	uint32_t count;
	uint32_t seen = 0U;

	if (config == NULL || !serial_kind_valid(kind) || port_id < 1U ||
		port_id > 4U || legacy_records(buffer, length,
						       EF800_LEGACY_SERIAL_RECORD_SIZE,
						       &count) != 0) {
		return -EINVAL;
	}
	for (uint32_t index = 0U; index < count; ++index) {
		size_t offset = sizeof(uint32_t) +
			(size_t)index * EF800_LEGACY_SERIAL_RECORD_SIZE;
		int32_t entry_id = (int32_t)sys_get_le32(&buffer[offset]);
		const uint8_t *payload = &buffer[offset + sizeof(uint32_t)];

		if (entry_id < 1 || entry_id > 4 ||
			(seen & BIT((uint32_t)entry_id)) != 0U ||
			!serial_payload_valid(payload)) {
			return -EINVAL;
		}
		seen |= BIT((uint32_t)entry_id);
		if ((uint8_t)entry_id == port_id) {
			config->baudrate = sys_get_le32(&payload[0]);
			config->data_bits = (uint8_t)sys_get_le32(&payload[4]);
			config->parity = (enum ef800_measurement_parity)
				sys_get_le32(&payload[8]);
			config->stop_bits = (uint8_t)sys_get_le32(&payload[12]);
			config->flow_control = (enum ef800_measurement_flow_control)
				sys_get_le32(&payload[16]);
		}
	}
	return (seen & BIT(port_id)) != 0U ? 0 : -ENOENT;
}

static int parse_text_integer(const uint8_t *buffer, size_t length,
				      int32_t *value, int32_t minimum, int32_t maximum)
{
	char text[64];
	char * const cursor = text;
	char *end;
	long parsed;

	if (buffer == NULL || value == NULL || length == 0U || length >= sizeof(text)) {
		return -EINVAL;
	}
	memcpy(text, buffer, length);
	text[length] = '\0';
	parsed = strtol(cursor, &end, 10);
	if (end == cursor || parsed < minimum || parsed > maximum) {
		return -EINVAL;
	}
	while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') {
		++end;
	}
	if (*end != '\0') {
		return -EINVAL;
	}
	*value = (int32_t)parsed;
	return 0;
}

int ef800_time_settings_encode(const struct ef800_time_settings *settings,
				       char *buffer, size_t capacity, size_t *length)
{
	int result;

	if (!valid_time_settings(settings) || buffer == NULL || length == NULL ||
		capacity == 0U) {
		return -EINVAL;
	}
	result = snprintf(buffer, capacity, "%ld %d\n",
			(long)settings->utc_offset_minutes,
			settings->daylight_saving ? 1 : 0);
	if (result < 0 || (size_t)result >= capacity) {
		return -ENOSPC;
	}
	*length = (size_t)result;
	return 0;
}

int ef800_time_settings_decode(const uint8_t *buffer, size_t length,
				       struct ef800_time_settings *settings)
{
	char text[64];
	char *cursor;
	char *end;
	long offset;
	int32_t daylight_saving;

	if (buffer == NULL || settings == NULL || length == 0U ||
		length >= sizeof(text) || memchr(buffer, '\0', length) != NULL) {
		return -EINVAL;
	}
	memcpy(text, buffer, length);
	text[length] = '\0';
	cursor = text;
	offset = strtol(cursor, &end, 10);
	if (end == cursor || offset < -24 * 60L || offset > 24 * 60L) {
		return -EINVAL;
	}
	cursor = end;
	while (*cursor == ' ' || *cursor == '\t') {
		++cursor;
	}
	if (parse_text_integer((const uint8_t *)cursor, strlen(cursor),
				       &daylight_saving, 0, 1) != 0) {
		return -EINVAL;
	}
	settings->utc_offset_minutes = (int32_t)offset;
	settings->daylight_saving = daylight_saving != 0;
	return 0;
}

static bool serial_config_valid(
	const struct ef800_measurement_serial_config *config)
{
	return config != NULL && config->baudrate > 0U &&
		config->baudrate <= 10000000U && config->data_bits >= 5U &&
		config->data_bits <= 8U && config->parity <= EF800_MEASUREMENT_PARITY_EVEN &&
		(config->stop_bits == 1U || config->stop_bits == 2U) &&
		config->flow_control >= EF800_MEASUREMENT_FLOW_NONE &&
		config->flow_control <= EF800_MEASUREMENT_FLOW_RTS_CTS;
}

int ef800_serial_config_encode(
	const struct ef800_measurement_serial_config *config, uint8_t *buffer,
	size_t capacity, size_t *length)
{
	if (!serial_config_valid(config) || buffer == NULL || length == NULL ||
		capacity < EF800_SERIAL_CONFIG_RECORD_SIZE) {
		return -EINVAL;
	}
	memset(buffer, 0, EF800_SERIAL_CONFIG_RECORD_SIZE);
	sys_put_le32(EF800_SERIAL_CONFIG_MAGIC, &buffer[0]);
	sys_put_le16(EF800_SERIAL_CONFIG_VERSION, &buffer[4]);
	sys_put_le16(EF800_SERIAL_CONFIG_RECORD_SIZE, &buffer[6]);
	sys_put_le32(config->baudrate, &buffer[8]);
	buffer[12] = config->data_bits;
	buffer[13] = (uint8_t)config->parity;
	buffer[14] = config->stop_bits;
	buffer[15] = (uint8_t)config->flow_control;
	sys_put_le32(ef800_crc32(buffer, 20U), &buffer[20]);
	*length = EF800_SERIAL_CONFIG_RECORD_SIZE;
	return 0;
}

int ef800_serial_config_decode(const uint8_t *buffer, size_t length,
				       struct ef800_measurement_serial_config *config)
{
	struct ef800_measurement_serial_config decoded;

	if (buffer == NULL || config == NULL ||
		length != EF800_SERIAL_CONFIG_RECORD_SIZE ||
		sys_get_le32(&buffer[0]) != EF800_SERIAL_CONFIG_MAGIC ||
		sys_get_le16(&buffer[4]) != EF800_SERIAL_CONFIG_VERSION ||
		sys_get_le16(&buffer[6]) != EF800_SERIAL_CONFIG_RECORD_SIZE ||
		sys_get_le32(&buffer[20]) != ef800_crc32(buffer, 20U) ||
		buffer[16] != 0U || buffer[17] != 0U || buffer[18] != 0U ||
		buffer[19] != 0U || buffer[15] > EF800_MEASUREMENT_FLOW_RTS_CTS) {
		return -EINVAL;
	}
	decoded.baudrate = sys_get_le32(&buffer[8]);
	decoded.data_bits = buffer[12];
	decoded.parity = (enum ef800_measurement_parity)buffer[13];
	decoded.stop_bits = buffer[14];
	decoded.flow_control = (enum ef800_measurement_flow_control)buffer[15];
	if (!serial_config_valid(&decoded)) {
		return -EINVAL;
	}
	*config = decoded;
	return 0;
}

int ef800_serial_config_load(struct ef800_persistence *storage,
				     enum ef800_legacy_serial_kind kind, uint8_t port_id,
				     struct ef800_measurement_serial_config *config)
{
	uint8_t buffer[EF800_SERIAL_CONFIG_RECORD_SIZE];
	char name[32];
	const char *prefix;
	size_t length;
	int result;

	if (storage == NULL || config == NULL || port_id < 1U || port_id > 4U ||
		(prefix = serial_config_prefix(kind)) == NULL) {
		return -EINVAL;
	}
	result = snprintf(name, sizeof(name), "%s%u", prefix, port_id);
	if (result < 0 || (size_t)result >= sizeof(name)) {
		return -ENAMETOOLONG;
	}
	result = read_file(storage, name, buffer, sizeof(buffer), &length);
	if (result != 0) {
		return result;
	}
	return ef800_serial_config_decode(buffer, length, config);
}

static const char *legacy_serial_file(enum ef800_legacy_serial_kind kind)
{
	switch (kind) {
	case EF800_LEGACY_SERIAL_DEBUG:
		return EF800_LEGACY_DEBUG_FILE;
	case EF800_LEGACY_SERIAL_ELTA3:
		return EF800_LEGACY_ELTA3_FILE;
	case EF800_LEGACY_SERIAL_FTS3XX:
		return EF800_LEGACY_FTS3XX_FILE;
	default:
		return NULL;
	}
}

static const char *serial_config_prefix(enum ef800_legacy_serial_kind kind)
{
	switch (kind) {
	case EF800_LEGACY_SERIAL_DEBUG:
		return EF800_DEBUG_CONFIG_FILE_PREFIX;
	case EF800_LEGACY_SERIAL_ELTA3:
		return EF800_ELTA3_CONFIG_FILE_PREFIX;
	case EF800_LEGACY_SERIAL_FTS3XX:
		return EF800_FTS3XX_CONFIG_FILE_PREFIX;
	default:
		return NULL;
	}
}

static int read_file(struct ef800_persistence *storage, const char *name,
                             uint8_t *buffer, size_t capacity, size_t *length)
{
	return ef800_persistence_read(storage, EF800_PERSISTENCE_CONFIGURATION, name,
				      buffer, capacity, length);
}

static int target_exists(struct ef800_persistence *storage, const char *name,
				 bool *exists)
{
	struct ef800_persistence_entry entry;
	int result;

	result = ef800_persistence_stat(storage, EF800_PERSISTENCE_CONFIGURATION,
					name, &entry);
	if (result == -ENOENT) {
		*exists = false;
		return 0;
	}
	if (result == 0) {
		*exists = true;
		return entry.type == EF800_PERSISTENCE_FILE ? 0 : -EISDIR;
	}
	return result;
}

static int legacy_assignment_preflight(struct ef800_persistence *storage)
{
	uint8_t buffer[EF800_LEGACY_MAX_FILE_SIZE];
	bool debug_assigned = false;

	for (uint8_t port_id = 1U; port_id <= 4U; ++port_id) {
		struct ef800_port_assignment_state state;
		struct ef800_port_assignment_state target_state;
		char name[16];
		char target_name[32];
		size_t length;
		int result;

		result = assignment_name(name, sizeof(name),
						EF800_LEGACY_PORT_ASSIGNMENT_FILE_PREFIX, port_id);
		if (result != 0 || assignment_name(target_name, sizeof(target_name),
						EF800_PORT_ASSIGNMENT_FILE_PREFIX, port_id) != 0) {
			return result != 0 ? result : -ENAMETOOLONG;
		}
		result = read_file(storage, name, buffer, sizeof(buffer), &length);
		if (result == 0) {
			result = parse_legacy_assignment(buffer, length, port_id, &state);
			if (result != 0) {
				return result;
			}
			result = read_file(storage, target_name, buffer, sizeof(buffer), &length);
			if (result == 0) {
				result = ef800_port_assignment_decode(buffer, length, &target_state);
				if (result != 0) {
					result = ef800_persistence_remove(storage,
						EF800_PERSISTENCE_CONFIGURATION, target_name);
					if (result != 0 && result != -ENOENT) {
						return result;
					}
				} else if (!assignment_equal(&state, &target_state)) {
					return -EEXIST;
				}
			} else if (result != -ENOENT) {
				return result;
			}
		} else if (result != -ENOENT) {
			return result;
		} else {
			result = read_file(storage, target_name, buffer, sizeof(buffer), &length);
			if (result == -ENOENT) {
				continue;
			}
			if (result != 0) {
				return -EBADMSG;
			}
			result = ef800_port_assignment_decode(buffer, length, &state);
			if (result != 0 || state.port_id != port_id) {
				result = ef800_persistence_remove(storage,
					EF800_PERSISTENCE_CONFIGURATION, target_name);
				if (result != 0 && result != -ENOENT) {
					return result;
				}
				continue;
			}
		}
		if (state.builder_id == EF800_PORT_BUILDER_DEBUG) {
			if (debug_assigned) {
				return -EADDRINUSE;
			}
			debug_assigned = true;
		}
	}
	return 0;
}

static int legacy_time_preflight(struct ef800_persistence *storage)
{
	uint8_t buffer[EF800_LEGACY_MAX_FILE_SIZE];
	struct ef800_time_settings settings;
	struct ef800_time_settings legacy_settings;
	struct ef800_persistence_entry entry;
	size_t length;
	size_t legacy_length;
	int result;

	result = ef800_persistence_stat(storage, EF800_PERSISTENCE_CONFIGURATION,
					EF800_TIME_SETTINGS_FILE, &entry);
	if (result == 0) {
		if (entry.type != EF800_PERSISTENCE_FILE) {
			return -EISDIR;
		}
		result = read_file(storage, EF800_TIME_SETTINGS_FILE, buffer,
					   sizeof(buffer), &length);
		if (result != 0) {
			return result;
		}
		result = ef800_time_settings_decode(buffer, length, &settings);
		if (result != 0) {
			result = read_file(storage, EF800_LEGACY_TIME_FILE, buffer, sizeof(buffer),
					   &legacy_length);
			if (result != 0 || ef800_legacy_parse_time(buffer, legacy_length,
									&legacy_settings) != 0) {
				return -EBADMSG;
			}
			result = ef800_persistence_remove(storage, EF800_PERSISTENCE_CONFIGURATION,
						  EF800_TIME_SETTINGS_FILE);
			return result == -ENOENT ? 0 : result;
		}
		result = read_file(storage, EF800_LEGACY_TIME_FILE, buffer, sizeof(buffer),
					   &legacy_length);
		if (result == -ENOENT) {
			return 0;
		}
		if (result != 0 || ef800_legacy_parse_time(buffer, legacy_length,
									&legacy_settings) != 0 ||
			legacy_settings.utc_offset_minutes != settings.utc_offset_minutes ||
			legacy_settings.daylight_saving != settings.daylight_saving) {
			return -EEXIST;
		}
		return 0;
	}
	if (result != -ENOENT) {
		return result;
	}
	result = read_file(storage, EF800_LEGACY_TIME_FILE, buffer, sizeof(buffer),
				   &length);
	return result == -ENOENT ? 0 : result == 0 ?
		ef800_legacy_parse_time(buffer, length, &settings) : result;
}

static int legacy_serial_preflight(struct ef800_persistence *storage,
					   enum ef800_legacy_serial_kind kind)
{
	uint8_t buffer[EF800_LEGACY_MAX_FILE_SIZE];
	uint8_t target[EF800_SERIAL_CONFIG_RECORD_SIZE];
	const char *source_name = legacy_serial_file(kind);
	const char *prefix = serial_config_prefix(kind);
	size_t length;
	size_t target_length;
	int result;

	result = read_file(storage, source_name, buffer, sizeof(buffer), &length);
	if (result == -ENOENT) {
		return 0;
	}
	if (result != 0) {
		return result;
	}
	for (uint8_t port_id = 1U; port_id <= 4U; ++port_id) {
		struct ef800_measurement_serial_config config;
		struct ef800_measurement_serial_config target_config;
		char name[16];

		result = ef800_legacy_parse_serial_config(buffer, length, kind, port_id,
									&config);
		if (result != 0 && result != -ENOENT) {
			return result;
		}
		if (result == 0) {
			if (assignment_name(name, sizeof(name), prefix, port_id) != 0) {
				return -ENAMETOOLONG;
			}
			result = read_file(storage, name, target, sizeof(target), &target_length);
			if (result == 0) {
				result = ef800_serial_config_decode(target, target_length,
									&target_config);
				if (result != 0) {
					result = ef800_persistence_remove(storage,
						EF800_PERSISTENCE_CONFIGURATION, name);
					if (result != 0 && result != -ENOENT) {
						return result;
					}
				} else if (!serial_config_equal(&config, &target_config)) {
					return -EEXIST;
				}
			} else if (result != -ENOENT) {
				return result;
			}
		}
	}
	return 0;
}

int ef800_legacy_migrate_time(struct ef800_persistence *storage)
{
	struct ef800_time_settings settings;
	struct ef800_time_settings verified;
	uint8_t source[EF800_LEGACY_MAX_FILE_SIZE];
	uint8_t output[64];
	size_t source_length;
	size_t output_length;
	size_t verify_length;
	bool exists;
	int result;

	if (storage == NULL) {
		return -EINVAL;
	}
	result = target_exists(storage, EF800_TIME_SETTINGS_FILE, &exists);
	if (result != 0) {
		return result;
	}
	if (exists) {
		result = read_file(storage, EF800_TIME_SETTINGS_FILE, output, sizeof(output),
					   &verify_length);
		if (result != 0 || ef800_time_settings_decode(output, verify_length,
									&verified) != 0) {
			return -EBADMSG;
		}
		result = read_file(storage, EF800_LEGACY_TIME_FILE, source, sizeof(source),
					   &source_length);
		if (result == -ENOENT) {
			return 0;
		}
		if (result != 0 || ef800_legacy_parse_time(source, source_length,
									&settings) != 0 ||
			settings.utc_offset_minutes != verified.utc_offset_minutes ||
			settings.daylight_saving != verified.daylight_saving) {
			return -EEXIST;
		}
		result = ef800_persistence_remove(storage, EF800_PERSISTENCE_CONFIGURATION,
						  EF800_LEGACY_TIME_FILE);
		return result == -ENOENT ? 0 : result;
	}
	result = read_file(storage, EF800_LEGACY_TIME_FILE, source, sizeof(source),
				  &source_length);
	if (result != 0) {
		return result;
	}
	result = ef800_legacy_parse_time(source, source_length, &settings);
	if (result != 0) {
		return result;
	}
	result = ef800_time_settings_encode(&settings, (char *)output,
						 sizeof(output), &output_length);
	if (result != 0) {
		return result;
	}
	result = ef800_persistence_write(storage, EF800_PERSISTENCE_CONFIGURATION,
						EF800_TIME_SETTINGS_FILE, output, output_length);
	if (result != 0) {
		return result;
	}
	result = read_file(storage, EF800_TIME_SETTINGS_FILE, output, sizeof(output),
				  &verify_length);
	if (result != 0 || ef800_time_settings_decode(output, verify_length,
									&verified) != 0 ||
			settings.utc_offset_minutes != verified.utc_offset_minutes ||
			settings.daylight_saving != verified.daylight_saving) {
		(void)ef800_persistence_remove(storage, EF800_PERSISTENCE_CONFIGURATION,
						EF800_TIME_SETTINGS_FILE);
		return -EIO;
	}
	result = ef800_persistence_remove(storage, EF800_PERSISTENCE_CONFIGURATION,
						  EF800_LEGACY_TIME_FILE);
	return result == -ENOENT ? 0 : result;
}

int ef800_legacy_migrate_port_assignment(struct ef800_persistence *storage,
						 uint8_t port_id)
{
	struct ef800_port_assignment_state state;
	struct ef800_port_assignment_state verified;
	uint8_t legacy[EF800_LEGACY_MAX_FILE_SIZE];
	uint8_t buffer[EF800_LEGACY_MAX_FILE_SIZE];
	uint8_t output[EF800_PORT_ASSIGNMENT_RECORD_SIZE];
	char name[32];
	char target_name[32];
	size_t length;
	size_t output_length;
	int result;

	if (storage == NULL || port_id < 1U || port_id > 4U) {
		return -EINVAL;
	}
	result = assignment_name(name, sizeof(name),
						EF800_LEGACY_PORT_ASSIGNMENT_FILE_PREFIX, port_id);
	if (result != 0 || assignment_name(target_name, sizeof(target_name),
						EF800_PORT_ASSIGNMENT_FILE_PREFIX, port_id) != 0) {
		return result != 0 ? result : -ENAMETOOLONG;
	}
	result = read_file(storage, name, legacy, sizeof(legacy), &length);
	if (result == -ENOENT) {
		result = read_file(storage, target_name, buffer, sizeof(buffer), &length);
		if (result != 0 || ef800_port_assignment_decode(buffer, length, &verified) != 0 ||
			verified.port_id != port_id) {
			return result == -ENOENT ? -ENOENT : -EBADMSG;
		}
		return 0;
	}
	if (result != 0) {
		return result;
	}
	result = parse_legacy_assignment(legacy, length, port_id, &state);
	if (result != 0) {
		return result;
	}
	result = read_file(storage, target_name, buffer, sizeof(buffer), &length);
	if (result == 0) {
		if (ef800_port_assignment_decode(buffer, length, &verified) != 0) {
			return -EBADMSG;
		}
		if (!assignment_equal(&state, &verified)) {
			return -EEXIST;
		}
		result = ef800_persistence_remove(storage, EF800_PERSISTENCE_CONFIGURATION,
						  name);
		return result == -ENOENT ? 0 : result;
	}
	if (result != -ENOENT) {
		return result;
	}
	result = ef800_port_assignment_encode(&state, output, sizeof(output),
						&output_length);
	if (result != 0) {
		return result;
	}
	result = ef800_persistence_write(storage, EF800_PERSISTENCE_CONFIGURATION,
						target_name, output, output_length);
	if (result != 0) {
		return result;
	}
	result = ef800_persistence_read(storage, EF800_PERSISTENCE_CONFIGURATION,
						target_name, buffer, sizeof(buffer), &length);
	if (result != 0 || ef800_port_assignment_decode(buffer, length, &verified) != 0 ||
		!assignment_equal(&state, &verified)) {
		(void)ef800_persistence_remove(storage, EF800_PERSISTENCE_CONFIGURATION,
						target_name);
		return -EIO;
	}
	result = ef800_persistence_remove(storage, EF800_PERSISTENCE_CONFIGURATION, name);
	return result == -ENOENT ? 0 : result;
}

int ef800_legacy_migrate_serial_config(
	struct ef800_persistence *storage, enum ef800_legacy_serial_kind kind,
	uint8_t port_id)
{
	struct ef800_measurement_serial_config config;
	struct ef800_measurement_serial_config verified;
	uint8_t source[EF800_LEGACY_MAX_FILE_SIZE];
	uint8_t output[EF800_SERIAL_CONFIG_RECORD_SIZE];
	char name[32];
	const char *source_name;
	const char *prefix;
	size_t source_length;
	size_t output_length;
	size_t verify_length;
	bool exists;
	int result;

	if (storage == NULL || port_id < 1U || port_id > 4U ||
		(source_name = legacy_serial_file(kind)) == NULL ||
		(prefix = serial_config_prefix(kind)) == NULL) {
		return -EINVAL;
	}
	result = snprintf(name, sizeof(name), "%s%u", prefix, port_id);
	if (result < 0 || (size_t)result >= sizeof(name)) {
		return -ENAMETOOLONG;
	}
	result = target_exists(storage, name, &exists);
	if (result != 0) {
		return result;
	}
	if (exists) {
		result = read_file(storage, name, output, sizeof(output), &verify_length);
		if (result != 0 || ef800_serial_config_decode(output, verify_length,
									&verified) != 0) {
			return -EBADMSG;
		}
		result = read_file(storage, source_name, source, sizeof(source), &source_length);
		if (result == -ENOENT) {
			return 0;
		}
		if (result != 0 || ef800_legacy_parse_serial_config(source, source_length,
									kind, port_id, &config) != 0 ||
			!serial_config_equal(&config, &verified)) {
			return -EEXIST;
		}
		return 0;
	}
	result = read_file(storage, source_name, source, sizeof(source), &source_length);
	if (result != 0) {
		return result;
	}
	result = ef800_legacy_parse_serial_config(source, source_length, kind, port_id,
								&config);
	if (result != 0) {
		return result;
	}
	result = ef800_serial_config_encode(&config, output, sizeof(output),
						&output_length);
	if (result != 0) {
		return result;
	}
	result = ef800_persistence_write(storage, EF800_PERSISTENCE_CONFIGURATION,
						name, output, output_length);
	if (result != 0) {
		return result;
	}
	result = read_file(storage, name, output, sizeof(output), &verify_length);
	if (result != 0 || ef800_serial_config_decode(output, verify_length,
									&verified) != 0 ||
		config.baudrate != verified.baudrate ||
		config.data_bits != verified.data_bits || config.parity != verified.parity ||
		config.stop_bits != verified.stop_bits ||
		config.flow_control != verified.flow_control) {
		(void)ef800_persistence_remove(storage, EF800_PERSISTENCE_CONFIGURATION,
						name);
		return -EIO;
	}
	return 0;
}

static int migrate_serial_file(struct ef800_persistence *storage,
				       enum ef800_legacy_serial_kind kind)
{
	uint8_t buffer[EF800_LEGACY_MAX_FILE_SIZE];
	const char *source_name = legacy_serial_file(kind);
	size_t length;
	bool has_record = false;
	int result;
	struct ef800_measurement_serial_config config;

	result = read_file(storage, source_name, buffer, sizeof(buffer), &length);
	if (result == -ENOENT) {
		return 0;
	}
	if (result != 0) {
		return result;
	}
	for (uint8_t port_id = 1U; port_id <= 4U; ++port_id) {
		result = ef800_legacy_parse_serial_config(buffer, length, kind, port_id,
								&config);
		if (result == 0) {
			has_record = true;
			result = ef800_legacy_migrate_serial_config(storage, kind, port_id);
			if (result != 0) {
				return result;
			}
		} else if (result != -ENOENT) {
			return result;
		}
	}
	if (!has_record) {
		return 0;
	}
	result = ef800_persistence_remove(storage, EF800_PERSISTENCE_CONFIGURATION,
						  source_name);
	return result == -ENOENT ? 0 : result;
}

int ef800_legacy_migrate_all(struct ef800_persistence *storage)
{
	int first_error = 0;
	int result;

	if (storage == NULL) {
		return -EINVAL;
	}
	result = legacy_assignment_preflight(storage);
	if (result != 0) {
		return result;
	}
	result = legacy_time_preflight(storage);
	if (result != 0) {
		return result;
	}
	for (enum ef800_legacy_serial_kind kind = EF800_LEGACY_SERIAL_DEBUG;
		 kind <= EF800_LEGACY_SERIAL_FTS3XX; ++kind) {
		result = legacy_serial_preflight(storage, kind);
		if (result != 0) {
			return result;
		}
	}
	result = ef800_legacy_migrate_time(storage);
	if (result != 0 && result != -ENOENT) {
		first_error = result;
	}
	for (uint8_t port_id = 1U; port_id <= 4U; ++port_id) {
		result = ef800_legacy_migrate_port_assignment(storage, port_id);
		if (result != 0 && result != -ENOENT && first_error == 0) {
			first_error = result;
		}
	}
	for (enum ef800_legacy_serial_kind kind = EF800_LEGACY_SERIAL_DEBUG;
		 kind <= EF800_LEGACY_SERIAL_FTS3XX; ++kind) {
		result = migrate_serial_file(storage, kind);
		if (result != 0 && first_error == 0) {
			first_error = result;
		}
	}
	return first_error;
}
