/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Configure measurement instruments and parse/store observations. */
#ifndef EF800_MEASUREMENT_H
#define EF800_MEASUREMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ef800_update.h"

#define EF800_MEASUREMENT_TEXT_MAX 48U
#define EF800_MEASUREMENT_RESPONSE_MAX 128U

struct ef800_workspace;

enum ef800_measurement_parity {
	EF800_MEASUREMENT_PARITY_NONE = 0,
	EF800_MEASUREMENT_PARITY_ODD,
	EF800_MEASUREMENT_PARITY_EVEN,
};

enum ef800_measurement_flow_control {
	EF800_MEASUREMENT_FLOW_NONE = 0,
	EF800_MEASUREMENT_FLOW_RTS,
	EF800_MEASUREMENT_FLOW_CTS,
	EF800_MEASUREMENT_FLOW_RTS_CTS,
};

struct ef800_measurement_serial_config {
	uint32_t baudrate;
	uint8_t data_bits;
	enum ef800_measurement_parity parity;
	uint8_t stop_bits;
	enum ef800_measurement_flow_control flow_control;
};

struct ef800_measurement_transport {
	int (*write)(void *context, const uint8_t *data, size_t length);
	int (*read)(void *context, uint8_t *data, int32_t timeout_ms);
	void *context;
	/* Absolute-deadline variants are required by the isolated worker gateway. */
	int (*write_until)(void *context, const uint8_t *data, size_t length,
		uint64_t deadline_ms);
	int (*read_until)(void *context, uint8_t *data, uint64_t deadline_ms);
};

struct ef800_measurement_data {
	/* Angles are gon multiplied by 10000; distance is millimetres. */
	int32_t distance_mm;
	int32_t horizontal_gon_1e4;
	int32_t vertical_gon_1e4;
	uint8_t control;
};

struct ef800_measurement_record {
	int32_t number;
	struct ef800_measurement_data data;
	int32_t staff_height_mm;
	char code[EF800_MEASUREMENT_TEXT_MAX + 1U];
};

/** @brief Fill the default serial configuration for an ELTA3 instrument.
 * @param config Receives the default UART settings.
 * @return None.
 */
void ef800_elta3_default_serial_config(
	struct ef800_measurement_serial_config *config);

/** @brief Fill the default serial configuration for an FTS3xx instrument.
 * @param config Receives the default UART settings.
 * @return None.
 */
void ef800_fts3xx_default_serial_config(
	struct ef800_measurement_serial_config *config);

/** @brief Resolve serial settings for a device builder and connection.
 * @param builder_id Instrument builder ID.
 * @param connection Selected connection ID.
 * @param config Receives settings when the device uses serial transport.
 * @return 0 on success; negative errno for unsupported builder/connection.
 */
int ef800_measurement_serial_config_for_builder(uint32_t builder_id,
	uint32_t connection, struct ef800_measurement_serial_config *config);

/** @brief Send the ELTA3 up command.
 * @param transport Instrument transport callbacks.
 * @return 0 on protocol success; negative errno on transport/protocol failure.
 */
int ef800_elta3_up(const struct ef800_measurement_transport *transport);

/** @brief Send the ELTA3 down command.
 * @param transport Instrument transport callbacks.
 * @return 0 on protocol success; negative errno on transport/protocol failure.
 */
int ef800_elta3_down(const struct ef800_measurement_transport *transport);

/** @brief Send the ELTA3 enter/confirm command.
 * @param transport Instrument transport callbacks.
 * @return 0 on protocol success; negative errno on transport/protocol failure.
 */
int ef800_elta3_enter(const struct ef800_measurement_transport *transport);

/** @brief Request and parse one ELTA3 measurement.
 * @param transport Instrument transport callbacks.
 * @param measurement Receives distance and angle data.
 * @return 0 on measurement; negative errno on transport or parse failure.
 */
int ef800_elta3_measure(const struct ef800_measurement_transport *transport,
				       struct ef800_measurement_data *measurement);

/** @brief Request and parse one FTS3xx measurement.
 * @param transport Instrument transport callbacks.
 * @param measurement Receives distance and angle data.
 * @return 0 on measurement; negative errno on transport or parse failure.
 */
int ef800_fts3xx_measure(const struct ef800_measurement_transport *transport,
					 struct ef800_measurement_data *measurement);

/** @brief Parse an ELTA3 response line into measurement values.
 * @param response NUL-terminated instrument response.
 * @param measurement Receives parsed values.
 * @return 0 on parse; negative errno for malformed or out-of-range fields.
 */
int ef800_elta3_parse_response(const char *response,
				       struct ef800_measurement_data *measurement);

/** @brief Parse an FTS3xx response line into measurement values.
 * @param response NUL-terminated instrument response.
 * @param measurement Receives parsed values.
 * @return 0 on parse; negative errno for malformed or out-of-range fields.
 */
int ef800_fts3xx_parse_response(const char *response,
					 struct ef800_measurement_data *measurement);

/** @brief Format measurement fields as one workspace record.
 * @param record Record fields to serialize.
 * @param buffer Destination text buffer.
 * @param capacity Buffer capacity.
 * @return Formatted byte count, or negative errno if invalid/too small.
 */
int ef800_measurement_format_record(
	const struct ef800_measurement_record *record, char *buffer, size_t capacity);

/** @brief Format and append a measurement record to a workspace.
 * @param workspace Selected workspace.
 * @param record Measurement record to append.
 * @return 0 on append; negative errno for invalid data or storage failure.
 */
int ef800_measurement_append(struct ef800_workspace *workspace,
					 const struct ef800_measurement_record *record);

#endif /* EF800_MEASUREMENT_H */
