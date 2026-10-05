/* SPDX-License-Identifier: Apache-2.0 */
#include "plugin_runtime.h"
#include "protocol.h"

#include "plugin_manager/plugin_abi.h"

static struct ef800_measurement_result last_result;
static int has_result;

static void init_display(struct ef800_measurement_display *display)
{
    ef800_measurement_display_init(display, "Elta3",
        EF800_MEASUREMENT_ACTION_MEASURE |
        EF800_MEASUREMENT_ACTION_UP | EF800_MEASUREMENT_ACTION_DOWN |
        EF800_MEASUREMENT_ACTION_ENTER);
    display->row_count = 4u;
    (void)ef800_measurement_display_text(display->rows[0], sizeof(display->rows[0]), "Measure");
    (void)ef800_measurement_display_text(display->rows[1], sizeof(display->rows[1]), "Up");
    (void)ef800_measurement_display_text(display->rows[2], sizeof(display->rows[2]), "Down");
    (void)ef800_measurement_display_text(display->rows[3], sizeof(display->rows[3]), "Enter");
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
    return ef800_protocol_write(ef800_runtime.port, (const uint8_t *)text,
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
    status = ef800_protocol_read_line(ef800_runtime.port, response, sizeof(response));
    if (status < 0 || response[0] != 'A' || response[1] != '\r' ||
        response[2] != '\n' || response[3] != '\0') return status < 0 ? status : PM_EIO;
    status = send("B\r\n");
    if (status != PM_OK) return status;
    status = ef800_protocol_read_line(ef800_runtime.port, response, sizeof(response));
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
    if (value == EF800_MEASUREMENT_INPUT_UP) return send("S12\r\n");
    if (value == EF800_MEASUREMENT_INPUT_DOWN) return send("S11\r\n");
    if (value == EF800_MEASUREMENT_INPUT_ENTER) return send("S14\r\n");
    return PM_ENOTSUP;
}

static int32_t render(void *instance, struct ef800_measurement_display *display)
{
    if (instance != &ef800_runtime.public_instance || display == 0) return PM_EINVAL;
    init_display(display);
    if (has_result) {
        (void)ef800_measurement_display_number(display->rows[4], sizeof(display->rows[4]),
            "Dmm", last_result.distance_mm);
        (void)ef800_measurement_display_number(display->rows[5], sizeof(display->rows[5]),
            "H", last_result.horizontal_gon_1e4);
        display->row_count = 6u;
    }
    return ef800_measurement_display_validate(display) == 0 ? PM_OK : PM_EOVERFLOW;
}

static int32_t handle_key(void *instance,
    const struct ef800_measurement_key_event *event, uint32_t *result)
{
    uint32_t action = 0u;
    int32_t status;

    if (instance != &ef800_runtime.public_instance || event == 0 || result == 0) {
        return PM_EINVAL;
    }
    *result = EF800_MEASUREMENT_KEY_NOT_CONSUMED;
    if (event->code == EF800_MEASUREMENT_KEY_BACK) {
        *result = EF800_MEASUREMENT_KEY_REQUEST_BACK;
        return PM_OK;
    }
    if (event->code != EF800_MEASUREMENT_KEY_CHARACTER) return PM_OK;
    if (event->character == 'u' || event->character == 'U') action = EF800_MEASUREMENT_INPUT_UP;
    if (event->character == 'd' || event->character == 'D') action = EF800_MEASUREMENT_INPUT_DOWN;
    if (event->character == 'e' || event->character == 'E') action = EF800_MEASUREMENT_INPUT_ENTER;
    if (action == 0u) return PM_OK;
    status = input(instance, action);
    *result = status == PM_OK ? EF800_MEASUREMENT_KEY_CONSUMED :
        EF800_MEASUREMENT_KEY_ERROR;
    return PM_OK;
}

const struct ef800_measurement_api ef800_plugin_api = {
    EF800_MEASUREMENT_API_VERSION, sizeof(struct ef800_measurement_api),
    defaults, measure, input, EF800_MEASUREMENT_UI_API_VERSION,
    sizeof(struct ef800_measurement_api),
    EF800_MEASUREMENT_ACTION_MEASURE | EF800_MEASUREMENT_ACTION_UP |
        EF800_MEASUREMENT_ACTION_DOWN | EF800_MEASUREMENT_ACTION_ENTER,
    render, handle_key, {0u, 0u}
};
