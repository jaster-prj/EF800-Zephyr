/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Implement the FTS3xx serial measurement plugin and UI. */
#include "plugin_runtime.h"
#include "protocol.h"

#include "plugin_manager/plugin_abi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum fennel_state {
    FENNEL_INSTRUMENT = 0,
    FENNEL_CODE,
    FENNEL_POINT,
    FENNEL_STAFF,
    FENNEL_PENDING,
};

static struct ef800_measurement_result last_result;
static struct ef800_measurement_context context;
static enum fennel_state state;
static uint8_t selected_action;
static uint8_t has_result;
static uint8_t record_ready;
static char code[sizeof(context.code)];
static char point[16];
static char staff[16];
static uint8_t editing_code;
static uint8_t editing_point;
static uint8_t editing_staff;

/* cppcheck-suppress constParameterCallback -- plugin ABI callback type is fixed as void * */
static int32_t defaults(void *instance, struct ef800_serial_defaults *value)
{
    if (instance != &ef800_runtime.public_instance || value == 0) return PM_EINVAL;
    value->struct_size = sizeof(*value);
    value->baudrate = 9600u;
    value->data_bits = 8u;
    value->parity = EF800_SERIAL_PARITY_NONE;
    value->stop_bits = 1u;
    value->flow_control = EF800_SERIAL_FLOW_NONE;
    memset(value->reserved, 0, sizeof(value->reserved));
    return PM_OK;
}

static uint32_t action_mask(void)
{
    switch (state) {
    case FENNEL_CODE: return EF800_MEASUREMENT_ACTION_CODE;
    case FENNEL_POINT: return EF800_MEASUREMENT_ACTION_POINT;
    case FENNEL_STAFF: return EF800_MEASUREMENT_ACTION_STAFF_HEIGHT;
    case FENNEL_PENDING:
        return EF800_MEASUREMENT_ACTION_SAVE | EF800_MEASUREMENT_ACTION_DISCARD |
            EF800_MEASUREMENT_ACTION_FIX;
    default: return EF800_MEASUREMENT_ACTION_MEASURE;
    }
}

/* cppcheck-suppress constParameterCallback -- plugin ABI callback type is fixed as void * */
static int32_t measure(void *instance, struct ef800_measurement_result *result)
{
    static const uint8_t command[] = {0x11u, '\r', '\n'};
    char response[128];
	int32_t status;
	char log_message[160];

	if (instance != &ef800_runtime.public_instance || result == 0) return PM_EINVAL;
	(void)ef800_plugin_worker_log(&ef800_runtime.binding,
		"FTS3XX: sending measurement request (11 0d 0a)",
		(uint32_t)strlen("FTS3XX: sending measurement request (11 0d 0a)"));
	status = ef800_protocol_write(&ef800_runtime.binding, command, sizeof(command));
	if (status != PM_OK) {
		(void)snprintf(log_message, sizeof(log_message),
			"FTS3XX: request write failed status=%ld", (long)status);
		(void)ef800_plugin_worker_log(&ef800_runtime.binding, log_message,
			(uint32_t)strlen(log_message));
		return status;
	}
	(void)ef800_plugin_worker_log(&ef800_runtime.binding,
		"FTS3XX: request sent; waiting for response line",
		(uint32_t)strlen("FTS3XX: request sent; waiting for response line"));
	status = ef800_protocol_read_line(&ef800_runtime.binding, response, sizeof(response));
	if (status < 0) {
		(void)snprintf(log_message, sizeof(log_message),
			"FTS3XX: response read failed status=%ld", (long)status);
		(void)ef800_plugin_worker_log(&ef800_runtime.binding, log_message,
			(uint32_t)strlen(log_message));
		return status;
	}
	(void)snprintf(log_message, sizeof(log_message),
		"FTS3XX: received %ld bytes: %.100s", (long)status, response);
	(void)ef800_plugin_worker_log(&ef800_runtime.binding, log_message,
		(uint32_t)strlen(log_message));
    result->struct_size = sizeof(*result);
    result->distance_mm = 0;
    result->horizontal_gon_1e4 = 0;
    result->vertical_gon_1e4 = 0;
    result->control = 0u;
    memset(result->reserved, 0, sizeof(result->reserved));
    result->flags = 0u;
    memset(result->reserved_words, 0, sizeof(result->reserved_words));
	status = ef800_protocol_fts3xx_parse(response, result);
	if (status == PM_OK) {
		last_result = *result;
		has_result = 1u;
		(void)snprintf(log_message, sizeof(log_message),
			"FTS3XX: parsed distance=%ld mm horizontal=%ld vertical=%ld (gon x10000)",
			(long)result->distance_mm, (long)result->horizontal_gon_1e4,
			(long)result->vertical_gon_1e4);
	} else {
		(void)snprintf(log_message, sizeof(log_message),
			"FTS3XX: response parse failed status=%ld", (long)status);
	}
	(void)ef800_plugin_worker_log(&ef800_runtime.binding, log_message,
		(uint32_t)strlen(log_message));
	return status;
}

static int append_scaled(char *buffer, uint32_t capacity, uint32_t *offset,
    int32_t value, uint32_t decimals)
{
    int32_t whole = value / (int32_t)(decimals == 4u ? 10000 : 1000);
    int32_t fraction = value % (int32_t)(decimals == 4u ? 10000 : 1000);
    int written = snprintf(buffer + *offset, capacity - *offset,
        decimals == 4u ? "%ld.%04ld" : "%ld.%03ld", (long)whole,
        (long)(fraction < 0 ? -fraction : fraction));
    if (written < 0 || (uint32_t)written >= capacity - *offset) return PM_EOVERFLOW;
    *offset += (uint32_t)written;
    return PM_OK;
}

/* cppcheck-suppress constParameterCallback -- plugin ABI callback type is fixed as void * */
static int32_t get_workspace_record(void *instance, char *buffer,
    uint32_t capacity, uint32_t *length)
{
    uint32_t offset = 0u;
    int written;
    int32_t status;
    long point_number;
    long staff_height;

    if (instance != &ef800_runtime.public_instance || buffer == 0 || length == 0)
        return PM_EINVAL;
    if (!record_ready || !has_result) return PM_ENOENT;
    point_number = strtol(point, 0, 10);
    staff_height = strtol(staff, 0, 10);
    written = snprintf(buffer, capacity, "%ld ", point_number);
    if (written < 0 || (uint32_t)written >= capacity) return PM_EOVERFLOW;
    offset = (uint32_t)written;
    status = append_scaled(buffer, capacity, &offset,
        last_result.horizontal_gon_1e4, 4u);
    if (status != PM_OK) return status;
    written = snprintf(buffer + offset, capacity - offset, " ");
    if (written < 0 || (uint32_t)written >= capacity - offset) return PM_EOVERFLOW;
    offset += (uint32_t)written;
    status = append_scaled(buffer, capacity, &offset, last_result.distance_mm, 3u);
    if (status != PM_OK) return status;
    written = snprintf(buffer + offset, capacity - offset, " ");
    if (written < 0 || (uint32_t)written >= capacity - offset) return PM_EOVERFLOW;
    offset += (uint32_t)written;
    status = append_scaled(buffer, capacity, &offset,
        last_result.vertical_gon_1e4, 4u);
    if (status != PM_OK) return status;
    written = snprintf(buffer + offset, capacity - offset, " %ld.%03ld %s\r\n",
        staff_height / 1000, staff_height % 1000, code);
    if (written < 0 || (uint32_t)written >= capacity - offset) return PM_EOVERFLOW;
    *length = offset + (uint32_t)written;
    return PM_OK;
}

static int32_t workspace_record_result(void *instance, int32_t status)
{
    if (instance != &ef800_runtime.public_instance) return PM_EINVAL;
    if (status != 0) return PM_OK;
    record_ready = 0u;
    editing_code = 0u;
    editing_point = 0u;
    editing_staff = 0u;
    if (state == FENNEL_PENDING && selected_action == 2u) {
        int32_t next = (int32_t)strtol(point, 0, 10);
        if (next < INT32_MAX) ++next;
        snprintf(point, sizeof(point), "%ld", (long)next);
        if (measure(instance, &last_result) != PM_OK) return PM_EIO;
        state = FENNEL_PENDING;
        selected_action = 0u;
    } else {
        state = FENNEL_INSTRUMENT;
        selected_action = 0u;
    }
    return PM_OK;
}

/* cppcheck-suppress constParameterCallback -- plugin ABI callback type is fixed as void * */
static int32_t set_context(void *instance,
    const struct ef800_measurement_context *value)
{
    if (instance != &ef800_runtime.public_instance || value == 0 ||
        value->struct_size < sizeof(*value)) return PM_EINVAL;
    context = *value;
    strncpy(code, value->code, sizeof(code) - 1u);
    code[sizeof(code) - 1u] = '\0';
    if (code[0] == '\0') strcpy(code, "02");
    snprintf(point, sizeof(point), "%ld", (long)value->point_number + 1L);
    strcpy(staff, "0");
    state = FENNEL_INSTRUMENT;
    selected_action = 0u;
    has_result = 0u;
    record_ready = 0u;
    return PM_OK;
}

static int32_t input(void *instance, uint32_t value)
{
    if (instance != &ef800_runtime.public_instance) return PM_EINVAL;
    if (state == FENNEL_PENDING && value == EF800_MEASUREMENT_INPUT_UP) {
        selected_action = selected_action == 0u ? 2u : selected_action - 1u;
        return PM_OK;
    }
    if (state == FENNEL_PENDING && value == EF800_MEASUREMENT_INPUT_DOWN) {
        selected_action = (selected_action + 1u) % 3u;
        return PM_OK;
    }
    if (value != EF800_MEASUREMENT_INPUT_ENTER) return PM_ENOTSUP;
    if (state == FENNEL_INSTRUMENT) {
		int32_t status = measure(instance, &last_result);

		if (status != PM_OK) return status;
		state = FENNEL_PENDING;
		selected_action = 0u;
    } else if (state == FENNEL_CODE) {
        state = FENNEL_POINT;
    } else if (state == FENNEL_POINT) {
        state = FENNEL_STAFF;
    } else if (state == FENNEL_STAFF) {
        editing_staff = 0u;
        int32_t status = measure(instance, &last_result);

        if (status != PM_OK) return status;
        state = FENNEL_PENDING;
        selected_action = 0u;
    } else if (state == FENNEL_PENDING) {
        if (selected_action == 1u) {
            has_result = 0u;
            state = FENNEL_INSTRUMENT;
        } else {
            record_ready = 1u;
        }
    }
    return PM_OK;
}

/* cppcheck-suppress constParameterCallback -- plugin ABI callback type is fixed as void * */
static int32_t render(void *instance, struct ef800_measurement_display *display)
{
    if (instance != &ef800_runtime.public_instance || display == 0) return PM_EINVAL;
    ef800_measurement_display_init(display, "Fennel FTS3XX", action_mask());
    display->selected_action = selected_action;
    display->row_count = 1u;
    if (state == FENNEL_INSTRUMENT) {
        (void)ef800_measurement_display_text(display->rows[0], sizeof(display->rows[0]), "Measure now");
        (void)ef800_measurement_display_text(display->footer,
			 sizeof(display->footer), "ENTER start measurement");
    } else if (state == FENNEL_CODE) {
        (void)ef800_measurement_display_text(display->rows[0], sizeof(display->rows[0]), "Code");
        strcpy(display->rows[0], "Last Code: ");
        strncpy(display->rows[0] + 11u, code, sizeof(display->rows[0]) - 12u);
        display->rows[0][sizeof(display->rows[0]) - 1u] = '\0';
        (void)ef800_measurement_display_text(display->footer,
            sizeof(display->footer), "ENTER keep  TYPE change");
    } else if (state == FENNEL_POINT) {
        snprintf(display->rows[0], sizeof(display->rows[0]), "Point number: %s", point);
        (void)ef800_measurement_display_text(display->footer,
            sizeof(display->footer), "ENTER accept  TYPE change");
    } else if (state == FENNEL_STAFF) {
        strcpy(display->rows[0], "Staff height: ");
        strncpy(display->rows[0] + 14u, staff, sizeof(display->rows[0]) - 15u);
        display->rows[0][sizeof(display->rows[0]) - 1u] = '\0';
        (void)ef800_measurement_display_text(display->footer,
            sizeof(display->footer), "TYPE value  ENTER accept");
    } else {
        display->row_count = 3u;
        (void)ef800_measurement_display_text(display->rows[0], sizeof(display->rows[0]), "Save");
        (void)ef800_measurement_display_text(display->rows[1], sizeof(display->rows[1]), "Discard");
        (void)ef800_measurement_display_text(display->rows[2], sizeof(display->rows[2]), "Fix measurement");
    }
    return ef800_measurement_display_validate(display) == 0 ? PM_OK : PM_EOVERFLOW;
}

static int32_t handle_key(void *instance,
    const struct ef800_measurement_key_event *event, uint32_t *result)
{
    if (instance != &ef800_runtime.public_instance || event == 0 || result == 0)
        return PM_EINVAL;
    *result = EF800_MEASUREMENT_KEY_NOT_CONSUMED;
    if (event->code == EF800_MEASUREMENT_KEY_BACK) {
        state = FENNEL_INSTRUMENT;
        *result = EF800_MEASUREMENT_KEY_REQUEST_BACK;
        return PM_OK;
    }
    if (event->code != EF800_MEASUREMENT_KEY_CHARACTER) return PM_OK;
    if (event->character == 0x08u || event->character == 0x7fu) {
        char *target = state == FENNEL_CODE ? code : state == FENNEL_POINT ? point : staff;
        size_t length = strlen(target);
        if (length != 0u && ((state == FENNEL_CODE && editing_code) ||
            (state == FENNEL_POINT && editing_point) ||
            (state == FENNEL_STAFF && editing_staff))) {
            target[length - 1u] = '\0';
            *result = EF800_MEASUREMENT_KEY_CONSUMED;
        } else {
            *result = EF800_MEASUREMENT_KEY_UNSUPPORTED;
        }
        return PM_OK;
    }
    if (event->character == '\r' || event->character == '\n') {
        *result = EF800_MEASUREMENT_KEY_CONSUMED;
        return input(instance, EF800_MEASUREMENT_INPUT_ENTER) == PM_OK ? PM_OK : PM_EIO;
    }
    if (state == FENNEL_CODE && event->character >= 0x20u && event->character < 0x7fu) {
        if (!editing_code) {
            code[0] = '\0';
            editing_code = 1u;
        }
        size_t length = strlen(code);
        if (length + 1u < sizeof(code)) { code[length] = (char)event->character; code[length + 1u] = '\0'; }
        *result = EF800_MEASUREMENT_KEY_CONSUMED;
    } else if ((state == FENNEL_POINT || state == FENNEL_STAFF) &&
        event->character >= '0' && event->character <= '9') {
        char *target = state == FENNEL_POINT ? point : staff;
        if (state == FENNEL_POINT && !editing_point) {
            point[0] = '\0';
            editing_point = 1u;
        }
        if (state == FENNEL_STAFF && !editing_staff) {
            staff[0] = '\0';
            editing_staff = 1u;
        }
        size_t length = strlen(target);
        if (length + 1u < 15u) { target[length] = (char)event->character; target[length + 1u] = '\0'; }
        *result = EF800_MEASUREMENT_KEY_CONSUMED;
    } else {
        *result = EF800_MEASUREMENT_KEY_UNSUPPORTED;
    }
    return PM_OK;
}

const struct ef800_measurement_api ef800_plugin_api = {
    EF800_MEASUREMENT_API_VERSION, sizeof(struct ef800_measurement_api),
    defaults, measure, input, EF800_MEASUREMENT_UI_API_VERSION,
    sizeof(struct ef800_measurement_api), EF800_MEASUREMENT_ACTION_MEASURE,
    render, handle_key, set_context, get_workspace_record,
    workspace_record_result, {0u}
};
