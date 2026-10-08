/* SPDX-License-Identifier: Apache-2.0 */
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

void ef800_elta3_default_serial_config(
	struct ef800_measurement_serial_config *config);
void ef800_fts3xx_default_serial_config(
	struct ef800_measurement_serial_config *config);
int ef800_measurement_serial_config_for_builder(uint32_t builder_id,
	uint32_t connection, struct ef800_measurement_serial_config *config);
int ef800_elta3_up(const struct ef800_measurement_transport *transport);
int ef800_elta3_down(const struct ef800_measurement_transport *transport);
int ef800_elta3_enter(const struct ef800_measurement_transport *transport);
int ef800_elta3_measure(const struct ef800_measurement_transport *transport,
				       struct ef800_measurement_data *measurement);

int ef800_fts3xx_measure(const struct ef800_measurement_transport *transport,
					 struct ef800_measurement_data *measurement);

int ef800_elta3_parse_response(const char *response,
				       struct ef800_measurement_data *measurement);
int ef800_fts3xx_parse_response(const char *response,
					 struct ef800_measurement_data *measurement);

int ef800_measurement_format_record(
	const struct ef800_measurement_record *record, char *buffer, size_t capacity);

int ef800_measurement_append(struct ef800_workspace *workspace,
					 const struct ef800_measurement_record *record);

#endif /* EF800_MEASUREMENT_H */
