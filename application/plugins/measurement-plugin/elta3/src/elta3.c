/* SPDX-License-Identifier: Apache-2.0 */
#include "plugin_runtime.h"
#include "protocol.h"

#include "plugin_manager/plugin_abi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct ef800_measurement_result last_result;
static int has_result;
static struct ef800_measurement_context measurement_context;
enum elta_state { ELTA_INSTRUMENT = 0, ELTA_CODE, ELTA_POINT, ELTA_STAFF, ELTA_PENDING };
static enum elta_state state;
static uint8_t selected_action;
static uint8_t record_ready;
static char code[sizeof(measurement_context.code)];
static char point[16];
static char staff[16];
static uint8_t editing_code;
static uint8_t editing_point;
static uint8_t editing_staff;
static int32_t measure(void *instance, struct ef800_measurement_result *result);

static uint32_t action_mask(void)
{
    if (state == ELTA_CODE) return EF800_MEASUREMENT_ACTION_CODE;
    if (state == ELTA_POINT) return EF800_MEASUREMENT_ACTION_POINT;
    if (state == ELTA_STAFF) return EF800_MEASUREMENT_ACTION_STAFF_HEIGHT;
    if (state == ELTA_PENDING) return EF800_MEASUREMENT_ACTION_SAVE |
        EF800_MEASUREMENT_ACTION_DISCARD | EF800_MEASUREMENT_ACTION_FIX;
    return EF800_MEASUREMENT_ACTION_MEASURE | EF800_MEASUREMENT_ACTION_UP |
        EF800_MEASUREMENT_ACTION_DOWN | EF800_MEASUREMENT_ACTION_ENTER;
}

static int append_scaled(char *buffer, uint32_t capacity, uint32_t *offset,
    int32_t value, uint32_t decimals)
{
    int32_t divisor = decimals == 4u ? 10000 : 1000;
    int written = snprintf(buffer + *offset, capacity - *offset,
        decimals == 4u ? "%ld.%04ld" : "%ld.%03ld", (long)(value / divisor),
        (long)labs(value % divisor));
    if (written < 0 || (uint32_t)written >= capacity - *offset) return PM_EOVERFLOW;
    *offset += (uint32_t)written;
    return PM_OK;
}

static int32_t set_context(void *instance,
    const struct ef800_measurement_context *context)
{
    if (instance != &ef800_runtime.public_instance || context == 0 ||
        context->struct_size < sizeof(*context)) return PM_EINVAL;
    measurement_context = *context;
    strncpy(code, context->code, sizeof(code) - 1u);
    code[sizeof(code) - 1u] = '\0';
    if (code[0] == '\0') strcpy(code, "02");
    snprintf(point, sizeof(point), "%ld", (long)context->point_number + 1L);
    strcpy(staff, "0");
    state = ELTA_INSTRUMENT;
    selected_action = 0u;
    record_ready = 0u;
    editing_code = 0u;
    editing_point = 0u;
    editing_staff = 0u;
    return PM_OK;
}

static int32_t get_workspace_record(void *instance, char *buffer,
    uint32_t capacity, uint32_t *length)
{
    uint32_t offset = 0u;
    int32_t status;
    int written;
    long point_number = strtol(point, 0, 10);
    long staff_height = strtol(staff, 0, 10);

    if (instance != &ef800_runtime.public_instance || buffer == 0 || length == 0)
        return PM_EINVAL;
    if (!record_ready || !has_result) return PM_ENOENT;
    written = snprintf(buffer, capacity, "%ld ", point_number);
    if (written < 0 || (uint32_t)written >= capacity) return PM_EOVERFLOW;
    offset = (uint32_t)written;
    status = append_scaled(buffer, capacity, &offset, last_result.horizontal_gon_1e4, 4u);
    if (status != PM_OK) return status;
    written = snprintf(buffer + offset, capacity - offset, " ");
    if (written < 0 || (uint32_t)written >= capacity - offset) return PM_EOVERFLOW;
    offset += (uint32_t)written;
    status = append_scaled(buffer, capacity, &offset, last_result.distance_mm, 3u);
    if (status != PM_OK) return status;
    written = snprintf(buffer + offset, capacity - offset, " ");
    if (written < 0 || (uint32_t)written >= capacity - offset) return PM_EOVERFLOW;
    offset += (uint32_t)written;
    status = append_scaled(buffer, capacity, &offset, last_result.vertical_gon_1e4, 4u);
    if (status != PM_OK) return status;
    written = snprintf(buffer + offset, capacity - offset, " %ld.%03ld %s\r\n",
        staff_height / 1000, labs(staff_height % 1000), code);
    if (written < 0 || (uint32_t)written >= capacity - offset) return PM_EOVERFLOW;
    *length = offset + (uint32_t)written;
    return PM_OK;
}

static int32_t workspace_record_result(void *instance, int32_t status)
{
    if (instance != &ef800_runtime.public_instance) return PM_EINVAL;
    if (status != 0) return PM_OK;
    record_ready = 0u;
    if (state == ELTA_PENDING && selected_action == 2u) {
        int32_t number = (int32_t)strtol(point, 0, 10);
        if (number < INT32_MAX) ++number;
        snprintf(point, sizeof(point), "%ld", (long)number);
        if (measure(instance, &last_result) != PM_OK) return PM_EIO;
        state = ELTA_PENDING;
        selected_action = 0u;
        editing_code = 0u;
        editing_point = 0u;
        return PM_OK;
    }
    state = ELTA_INSTRUMENT;
    selected_action = 0u;
    return PM_OK;
}

static int32_t defaults(void *instance, struct ef800_serial_defaults *value)
{
    uint32_t index;
    if (instance != &ef800_runtime.public_instance || value == 0) return PM_EINVAL;
    value->struct_size = sizeof(*value);
    value->baudrate = 1200u;
    value->data_bits = 7u;
    value->parity = EF800_SERIAL_PARITY_ODD;
    value->stop_bits = 2u;
    value->flow_control = EF800_SERIAL_FLOW_NONE;
    for (index = 0u; index < 4u; ++index) value->reserved[index] = 0u;
    return PM_OK;
}

static int32_t send(const char *text)
{
    return ef800_protocol_write(&ef800_runtime.binding, (const uint8_t *)text,
                                ef800_protocol_length(text));
}

static int32_t measure(void *instance, struct ef800_measurement_result *result)
{
    char response[128];
    int32_t status;
    uint32_t index;
    if (instance != &ef800_runtime.public_instance || result == 0) return PM_EINVAL;
    status = send("S14\r\n");
    if (status != PM_OK) return status;
    status = ef800_protocol_read_line(&ef800_runtime.binding, response, sizeof(response));
    if (status < 0 || response[0] != 'A' || response[1] != '\r' ||
        response[2] != '\n' || response[3] != '\0') return status < 0 ? status : PM_EIO;
    status = send("B\r\n");
    if (status != PM_OK) return status;
    status = ef800_protocol_read_line(&ef800_runtime.binding, response, sizeof(response));
    if (status < 0) return status;
    status = send("Q\r\n");
    if (status != PM_OK) return status;
    result->struct_size = sizeof(*result);
    result->distance_mm = 0;
    result->horizontal_gon_1e4 = 0;
    result->vertical_gon_1e4 = 0;
    result->control = 0u;
    for (index = 0u; index < 3u; ++index) result->reserved[index] = 0u;
    result->flags = 0u;
    for (index = 0u; index < 3u; ++index) result->reserved_words[index] = 0u;
    status = ef800_protocol_elta3_parse(response, result);
    if (status == PM_OK) {
        last_result = *result;
        has_result = 1;
    }
    return status;
}

static int32_t input(void *instance, uint32_t value)
{
    if (instance != &ef800_runtime.public_instance) return PM_EINVAL;
    if (state == ELTA_INSTRUMENT && value == EF800_MEASUREMENT_INPUT_UP) {
        selected_action = selected_action == 0u ? 3u : selected_action - 1u;
        return PM_OK;
    }
    if (state == ELTA_INSTRUMENT && value == EF800_MEASUREMENT_INPUT_DOWN) {
        selected_action = (selected_action + 1u) % 4u;
        return PM_OK;
    }
    if (state == ELTA_PENDING && value == EF800_MEASUREMENT_INPUT_UP) {
        selected_action = selected_action == 0u ? 2u : selected_action - 1u;
        return PM_OK;
    }
    if (state == ELTA_PENDING && value == EF800_MEASUREMENT_INPUT_DOWN) {
        selected_action = (selected_action + 1u) % 3u;
        return PM_OK;
    }
    if (value != EF800_MEASUREMENT_INPUT_ENTER) return PM_ENOTSUP;
    if (state == ELTA_INSTRUMENT) {
        if (selected_action == 0u) {
            state = ELTA_CODE;
            editing_code = 0u;
            editing_point = 0u;
        }
        else if (selected_action == 1u) return send("S12\r\n");
        else if (selected_action == 2u) return send("S11\r\n");
        else return send("S14\r\n");
    } else if (state == ELTA_CODE) {
        state = ELTA_POINT;
        editing_point = 0u;
    } else if (state == ELTA_POINT) {
        state = ELTA_STAFF;
        editing_staff = 0u;
    } else if (state == ELTA_STAFF) {
        if (measure(instance, &last_result) != PM_OK) return PM_EIO;
        state = ELTA_PENDING;
        selected_action = 0u;
    } else if (state == ELTA_PENDING) {
        if (selected_action == 1u) {
            has_result = 0;
            state = ELTA_INSTRUMENT;
            selected_action = 0u;
        } else {
            record_ready = 1u;
        }
    }
    return PM_OK;
}

static int32_t render(void *instance, struct ef800_measurement_display *display)
{
    if (instance != &ef800_runtime.public_instance || display == 0) return PM_EINVAL;
    ef800_measurement_display_init(display, "Elta3", action_mask());
    display->selected_action = selected_action;
    if (state == ELTA_INSTRUMENT) {
        display->row_count = 4u;
        (void)ef800_measurement_display_text(display->rows[0], sizeof(display->rows[0]), "Measure");
        (void)ef800_measurement_display_text(display->rows[1], sizeof(display->rows[1]), "Up");
        (void)ef800_measurement_display_text(display->rows[2], sizeof(display->rows[2]), "Down");
        (void)ef800_measurement_display_text(display->rows[3], sizeof(display->rows[3]), "Enter");
    } else if (state == ELTA_CODE) {
        display->row_count = 1u;
        strcpy(display->rows[0], "Last Code: ");
        strncpy(display->rows[0] + 11u, code, sizeof(display->rows[0]) - 12u);
        display->rows[0][sizeof(display->rows[0]) - 1u] = '\0';
        (void)ef800_measurement_display_text(display->footer,
            sizeof(display->footer), "ENTER keep  TYPE change");
    } else if (state == ELTA_POINT) {
        display->row_count = 1u;
        snprintf(display->rows[0], sizeof(display->rows[0]), "Point number: %s", point);
        (void)ef800_measurement_display_text(display->footer,
            sizeof(display->footer), "ENTER accept  TYPE change");
    } else if (state == ELTA_STAFF) {
        display->row_count = 1u;
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
    if (instance != &ef800_runtime.public_instance || event == 0 || result == 0) {
        return PM_EINVAL;
    }
    *result = EF800_MEASUREMENT_KEY_NOT_CONSUMED;
    if (event->code == EF800_MEASUREMENT_KEY_BACK) {
        state = ELTA_INSTRUMENT;
        *result = EF800_MEASUREMENT_KEY_REQUEST_BACK;
        return PM_OK;
    }
    if (event->code != EF800_MEASUREMENT_KEY_CHARACTER) return PM_OK;
    if (event->character == 0x08u || event->character == 0x7fu) {
        char *target = state == ELTA_CODE ? code : state == ELTA_POINT ? point : staff;
        size_t length = strlen(target);
        if (length != 0u && ((state == ELTA_CODE && editing_code) ||
            (state == ELTA_POINT && editing_point) ||
            (state == ELTA_STAFF && editing_staff))) {
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
    if (state == ELTA_INSTRUMENT &&
        (event->character == 'u' || event->character == 'U' ||
         event->character == 'd' || event->character == 'D' ||
         event->character == 'e' || event->character == 'E')) {
        int32_t status = event->character == 'u' || event->character == 'U' ?
            send("S12\r\n") : event->character == 'd' || event->character == 'D' ?
            send("S11\r\n") : send("S14\r\n");
        *result = status == PM_OK ? EF800_MEASUREMENT_KEY_CONSUMED :
            EF800_MEASUREMENT_KEY_ERROR;
        return PM_OK;
    }
    if (state == ELTA_CODE && event->character >= 0x20u && event->character < 0x7fu) {
        if (!editing_code) {
            code[0] = '\0';
            editing_code = 1u;
        }
        size_t length = strlen(code);
        if (length + 1u < sizeof(code)) { code[length] = (char)event->character; code[length + 1u] = '\0'; }
        *result = EF800_MEASUREMENT_KEY_CONSUMED;
    } else if ((state == ELTA_POINT || state == ELTA_STAFF) &&
        event->character >= '0' && event->character <= '9') {
        char *target = state == ELTA_POINT ? point : staff;
        if (state == ELTA_POINT && !editing_point) {
            point[0] = '\0';
            editing_point = 1u;
        }
        if (state == ELTA_STAFF && !editing_staff) {
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
    sizeof(struct ef800_measurement_api),
    EF800_MEASUREMENT_ACTION_MEASURE | EF800_MEASUREMENT_ACTION_UP |
        EF800_MEASUREMENT_ACTION_DOWN | EF800_MEASUREMENT_ACTION_ENTER,
    render, handle_key, set_context, get_workspace_record,
    workspace_record_result, {0u}
};
