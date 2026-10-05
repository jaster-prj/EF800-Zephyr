/* SPDX-License-Identifier: Apache-2.0 */
#include "plugin_runtime.h"
#include "protocol.h"

#include "plugin_manager/plugin_abi.h"

static struct ef800_measurement_result last_result;
static int has_result;

static int32_t defaults(void *instance, struct ef800_serial_defaults *value)
{
    uint32_t index;
    if (instance != &ef800_runtime.public_instance || value == 0) return PM_EINVAL;
    value->struct_size = sizeof(*value);
    value->baudrate = 9600u;
    value->data_bits = 8u;
    value->parity = EF800_SERIAL_PARITY_NONE;
    value->stop_bits = 1u;
    value->flow_control = EF800_SERIAL_FLOW_NONE;
    for (index = 0u; index < 4u; ++index) value->reserved[index] = 0u;
    return PM_OK;
}

static int32_t measure(void *instance, struct ef800_measurement_result *result)
{
    static const uint8_t command[] = {0x11u, '\r', '\n'};
    char response[128];
    int32_t status;
    uint32_t index;
    if (instance != &ef800_runtime.public_instance || result == 0) return PM_EINVAL;
    status = ef800_protocol_write(ef800_runtime.port, command, sizeof(command));
    if (status != PM_OK) return status;
    status = ef800_protocol_read_line(ef800_runtime.port, response, sizeof(response));
    if (status < 0) return status;
    result->struct_size = sizeof(*result);
    result->distance_mm = 0;
    result->horizontal_gon_1e4 = 0;
    result->vertical_gon_1e4 = 0;
    result->control = 0u;
    for (index = 0u; index < 3u; ++index) result->reserved[index] = 0u;
    result->flags = 0u;
    for (index = 0u; index < 3u; ++index) result->reserved_words[index] = 0u;
    status = ef800_protocol_fts3xx_parse(response, result);
    if (status == PM_OK) {
        last_result = *result;
        has_result = 1;
    }
    return status;
}

static int32_t input(void *instance, uint32_t value)
{
    (void)value;
    return instance == &ef800_runtime.public_instance ? PM_ENOTSUP : PM_EINVAL;
}

static int32_t render(void *instance, struct ef800_measurement_display *display)
{
    if (instance != &ef800_runtime.public_instance || display == 0) return PM_EINVAL;
    ef800_measurement_display_init(display, "Fennel1",
        EF800_MEASUREMENT_ACTION_MEASURE);
    display->row_count = 1u;
    (void)ef800_measurement_display_text(display->rows[0], sizeof(display->rows[0]),
        "Measure");
    if (has_result) {
        (void)ef800_measurement_display_number(display->rows[1], sizeof(display->rows[1]),
            "Dmm", last_result.distance_mm);
        (void)ef800_measurement_display_number(display->rows[2], sizeof(display->rows[2]),
            "H", last_result.horizontal_gon_1e4);
        display->row_count = 3u;
    }
    return ef800_measurement_display_validate(display) == 0 ? PM_OK : PM_EOVERFLOW;
}

static int32_t handle_key(void *instance,
    const struct ef800_measurement_key_event *event, uint32_t *result)
{
    if (instance != &ef800_runtime.public_instance || event == 0 || result == 0) {
        return PM_EINVAL;
    }
    if (event->code == EF800_MEASUREMENT_KEY_BACK) {
        *result = EF800_MEASUREMENT_KEY_REQUEST_BACK;
    } else if (event->code == EF800_MEASUREMENT_KEY_CHARACTER &&
        (event->character == 'm' || event->character == 'M')) {
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
    render, handle_key, {0u, 0u}
};
