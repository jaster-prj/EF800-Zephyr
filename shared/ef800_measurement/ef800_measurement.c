/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_measurement.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/sys/util.h>

#include "ef800_workspace.h"

static int transport_write(const struct ef800_measurement_transport *transport,
					   const uint8_t *data, size_t length)
{
	int result;

	if (transport == NULL || transport->write == NULL ||
		(data == NULL && length != 0U)) {
		return -EINVAL;
	}
	result = transport->write(transport->context, data, length);
	return result < 0 ? result : (size_t)result == length ? 0 : -EIO;
}

static int read_line(const struct ef800_measurement_transport *transport,
				 char *buffer, size_t capacity)
{
	size_t length = 0U;

	if (transport == NULL || transport->read == NULL || buffer == NULL ||
		capacity < 2U) {
		return -EINVAL;
	}
	while (length + 1U < capacity) {
		uint8_t byte;
		int result = transport->read(transport->context, &byte, 1000);

		if (result < 0) {
			return result;
		}
		if (result == 0) {
			return -ETIMEDOUT;
		}
		buffer[length++] = (char)byte;
		if (byte == '\n') {
			buffer[length] = '\0';
			return (int)length;
		}
	}
	return -ENOBUFS;
}

static int send_ascii(const struct ef800_measurement_transport *transport,
				      const char *command)
{
	return transport_write(transport, (const uint8_t *)command, strlen(command));
}

static int parse_scaled_decimal(const char *text, int32_t scale,
						int32_t *value)
{
	int64_t whole = 0;
	int64_t fraction = 0;
	int64_t divisor = 1;
	int sign = 1;
	bool decimal = false;
	bool digit = false;
	const char *cursor;

	if (text == NULL || value == NULL || scale <= 0 || text[0] == '\0') {
		return -EINVAL;
	}
	cursor = text;
	if (*cursor == '-' || *cursor == '+') {
		sign = *cursor++ == '-' ? -1 : 1;
	}
	for (; *cursor != '\0'; ++cursor) {
		char character = *cursor;

		if (character == ';') {
			continue;
		}
		if (character == '.') {
			if (decimal) {
				return -EINVAL;
			}
			decimal = true;
			continue;
		}
		if (character < '0' || character > '9') {
			return -EINVAL;
		}
		digit = true;
		if (!decimal) {
			if (whole > (INT64_MAX - (character - '0')) / 10) {
				return -ERANGE;
			}
			whole = whole * 10 + (character - '0');
		} else if (divisor < (int64_t)scale) {
			fraction = fraction * 10 + (character - '0');
			divisor *= 10;
		} else if (character != '0') {
			return -EINVAL;
		}
	}
	if (!digit) {
		return -EINVAL;
	}
	while (divisor < scale) {
		fraction *= 10;
		divisor *= 10;
	}
	whole = whole * scale + fraction;
	whole *= sign;
	if (whole < INT32_MIN || whole > INT32_MAX) {
		return -ERANGE;
	}
	*value = (int32_t)whole;
	return 0;
}

static int parse_control(const char *token, uint8_t *control)
{
	if (token == NULL || control == NULL || strlen(token) < 3U ||
		token[1] < '0' || token[1] > '9' || token[2] < '0' ||
		token[2] > '9') {
		return -EINVAL;
	}
	*control = (uint8_t)((token[1] - '0') * 10 + token[2] - '0');
	return *control == 33U || *control == 48U ? 0 : -EINVAL;
}

static size_t split_response(char *response, char *tokens[], size_t capacity)
{
	size_t count = 0U;
	char *cursor = response;

	while (*cursor != '\0' && count < capacity) {
		while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
		       *cursor == '\n') {
			++cursor;
		}
		if (*cursor == '\0') {
			break;
		}
		tokens[count++] = cursor;
		while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t' &&
		       *cursor != '\r' && *cursor != '\n') {
			++cursor;
		}
		if (*cursor != '\0') {
			*cursor++ = '\0';
		}
	}
	return count;
}

void ef800_elta3_default_serial_config(
	struct ef800_measurement_serial_config *config)
{
	if (config == NULL) {
		return;
	}
	*config = (struct ef800_measurement_serial_config){
		.baudrate = 1200U,
		.data_bits = 7U,
		.parity = EF800_MEASUREMENT_PARITY_ODD,
		.stop_bits = 2U,
		.flow_control = EF800_MEASUREMENT_FLOW_NONE,
	};
}

void ef800_fts3xx_default_serial_config(
	struct ef800_measurement_serial_config *config)
{
	if (config == NULL) {
		return;
	}
	*config = (struct ef800_measurement_serial_config){
		.baudrate = 9600U,
		.data_bits = 8U,
		.parity = EF800_MEASUREMENT_PARITY_NONE,
		.stop_bits = 1U,
		.flow_control = EF800_MEASUREMENT_FLOW_NONE,
	};
}

int ef800_measurement_serial_config_for_builder(
	uint8_t builder_id, uint32_t connection,
	struct ef800_measurement_serial_config *config)
{
	if (config == NULL || (connection != EF800_PORT_CONNECTION_SERIAL &&
		connection != EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL)) {
		return -EINVAL;
	}
	if (builder_id == EF800_PORT_BUILDER_ELTA3) {
		ef800_elta3_default_serial_config(config);
	} else if (builder_id == EF800_PORT_BUILDER_FTS3XX) {
		ef800_fts3xx_default_serial_config(config);
	} else {
		return -EINVAL;
	}
	config->flow_control = connection ==
		EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL ?
		EF800_MEASUREMENT_FLOW_RTS_CTS : EF800_MEASUREMENT_FLOW_NONE;
	return 0;
}

int ef800_elta3_up(const struct ef800_measurement_transport *transport)
{
	return send_ascii(transport, "S12\r\n");
}

int ef800_elta3_down(const struct ef800_measurement_transport *transport)
{
	return send_ascii(transport, "S11\r\n");
}

int ef800_elta3_enter(const struct ef800_measurement_transport *transport)
{
	return send_ascii(transport, "S14\r\n");
}

int ef800_elta3_parse_response(const char *response,
				       struct ef800_measurement_data *measurement)
{
	char copy[EF800_MEASUREMENT_RESPONSE_MAX];
	char *tokens[5];
	size_t count;
	int result;

	if (response == NULL || measurement == NULL ||
		strlen(response) >= sizeof(copy)) {
		return -EINVAL;
	}
	strcpy(copy, response);
	count = split_response(copy, tokens, ARRAY_SIZE(tokens));
	if (count == 0U || parse_control(tokens[0], &measurement->control) != 0) {
		return -EINVAL;
	}
	measurement->distance_mm = 0;
	if (measurement->control == 33U) {
		if (count < 3U) {
			return -EINVAL;
		}
		result = parse_scaled_decimal(tokens[1], 10000, &measurement->horizontal_gon_1e4);
		if (result == 0) {
			result = parse_scaled_decimal(tokens[2], 10000,
									 &measurement->vertical_gon_1e4);
		}
		return result;
	}
	if (count < 5U) {
		return -EINVAL;
	}
	result = parse_scaled_decimal(tokens[2], 1000, &measurement->distance_mm);
	if (result == 0) {
		result = parse_scaled_decimal(tokens[3], 10000,
								 &measurement->horizontal_gon_1e4);
	}
	if (result == 0) {
		result = parse_scaled_decimal(tokens[4], 10000,
								 &measurement->vertical_gon_1e4);
	}
	return result;
}

int ef800_elta3_measure(const struct ef800_measurement_transport *transport,
				       struct ef800_measurement_data *measurement)
{
	char response[EF800_MEASUREMENT_RESPONSE_MAX];
	int result;

	result = ef800_elta3_enter(transport);
	if (result != 0) {
		return result;
	}
	result = read_line(transport, response, sizeof(response));
	if (result < 0 || strcmp(response, "A\r\n") != 0) {
		return result < 0 ? result : -EIO;
	}
	result = send_ascii(transport, "B\r\n");
	if (result != 0) {
		return result;
	}
	result = read_line(transport, response, sizeof(response));
	if (result < 0) {
		return result;
	}
	result = send_ascii(transport, "Q\r\n");
	if (result != 0) {
		return result;
	}
	return ef800_elta3_parse_response(response, measurement);
}

static int parse_fts_angle(const char *text, int32_t *angle_gon_1e4)
{
	int32_t degrees;
	int32_t minutes;
	int32_t seconds;
	int64_t arc_seconds;

	if (text == NULL || angle_gon_1e4 == NULL || strlen(text) != 7U) {
		return -EINVAL;
	}
	for (size_t index = 0U; index < 7U; ++index) {
		if (text[index] < '0' || text[index] > '9') {
			return -EINVAL;
		}
	}
	degrees = (text[0] - '0') * 100 + (text[1] - '0') * 10 + text[2] - '0';
	minutes = (text[3] - '0') * 10 + text[4] - '0';
	seconds = (text[5] - '0') * 10 + text[6] - '0';
	if (minutes >= 60 || seconds >= 60) {
		return -EINVAL;
	}
	arc_seconds = (int64_t)degrees * 3600 + minutes * 60 + seconds;
	/* gon = degrees * 10 / 9; retain four fractional digits. */
	arc_seconds *= 100000;
	arc_seconds /= 32400;
	if (arc_seconds < INT32_MIN || arc_seconds > INT32_MAX) {
		return -ERANGE;
	}
	*angle_gon_1e4 = (int32_t)arc_seconds;
	return 0;
}

int ef800_fts3xx_parse_response(const char *response,
					 struct ef800_measurement_data *measurement)
{
	char copy[EF800_MEASUREMENT_RESPONSE_MAX];
	char *tokens[5];
	size_t count;
	int32_t distance;
	int result;

	if (response == NULL || measurement == NULL ||
		strlen(response) >= sizeof(copy)) {
		return -EINVAL;
	}
	strcpy(copy, response);
	count = split_response(copy, tokens, ARRAY_SIZE(tokens));
	if (count < 3U) {
		return -EINVAL;
	}
	result = parse_scaled_decimal(tokens[0], 1, &distance);
	if (result != 0) {
		return result;
	}
	result = parse_fts_angle(tokens[1], &measurement->vertical_gon_1e4);
	if (result == 0) {
		result = parse_fts_angle(tokens[2], &measurement->horizontal_gon_1e4);
	}
	if (result == 0) {
		measurement->distance_mm = distance;
		measurement->control = 0U;
	}
	return result;
}

int ef800_fts3xx_measure(const struct ef800_measurement_transport *transport,
					 struct ef800_measurement_data *measurement)
{
	static const uint8_t command[] = {0x11U, '\r', '\n'};
	char response[EF800_MEASUREMENT_RESPONSE_MAX];
	int result;

	result = transport_write(transport, command, sizeof(command));
	if (result != 0) {
		return result;
	}
	result = read_line(transport, response, sizeof(response));
	return result < 0 ? result : ef800_fts3xx_parse_response(response, measurement);
}

static int format_scaled(char *buffer, size_t capacity, int32_t value,
					 int32_t scale, uint8_t fraction_digits)
{
	int64_t absolute = value < 0 ? -(int64_t)value : value;
	int64_t fraction_scale = 1;
	int length;

	for (uint8_t index = 0U; index < fraction_digits; ++index) {
		fraction_scale *= 10;
	}

	length = snprintf(buffer, capacity, "%s%lld.%0*lld", value < 0 ? "-" : "",
			(long long)(absolute / scale), fraction_digits,
			(long long)((absolute % scale) * fraction_scale / scale));
	return length < 0 || (size_t)length >= capacity ? -ENOSPC : length;
}

static bool measurement_code_valid(const char *code)
{
	if (code == NULL || code[0] == '\0' || strlen(code) > EF800_MEASUREMENT_TEXT_MAX) {
		return false;
	}
	for (const char *cursor = code; *cursor != '\0'; ++cursor) {
		if (*cursor <= ' ' || *cursor == '\r' || *cursor == '\n') {
			return false;
		}
	}
	return true;
}

int ef800_measurement_format_record(
	const struct ef800_measurement_record *record, char *buffer, size_t capacity)
{
	char horizontal[24];
	char vertical[24];
	char distance[24];
	char staff_height[24];
	int length;

	if (record == NULL || buffer == NULL || capacity == 0U || record->number < 0 ||
		!measurement_code_valid(record->code)) {
		return -EINVAL;
	}
	if (format_scaled(horizontal, sizeof(horizontal),
				  record->data.horizontal_gon_1e4, 10000, 4U) < 0 ||
		format_scaled(vertical, sizeof(vertical), record->data.vertical_gon_1e4,
				 10000, 4U) < 0 ||
		format_scaled(distance, sizeof(distance), record->data.distance_mm,
				 1000, 3U) < 0 ||
		format_scaled(staff_height, sizeof(staff_height), record->staff_height_mm,
				 1000, 3U) < 0) {
		return -ERANGE;
	}
	length = snprintf(buffer, capacity, "%ld %s %s %s %s %s\r",
			(long)record->number, horizontal, distance, vertical, staff_height,
			record->code);
	return length < 0 || (size_t)length >= capacity ? -ENOSPC : length;
}

int ef800_measurement_append(struct ef800_workspace *workspace,
					 const struct ef800_measurement_record *record)
{
	char buffer[128];
	int length;

	length = ef800_measurement_format_record(record, buffer, sizeof(buffer));
	if (length < 0) {
		return length;
	}
	return ef800_workspace_append(workspace, buffer, (size_t)length);
}
