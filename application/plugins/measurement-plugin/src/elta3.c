/* SPDX-License-Identifier: Apache-2.0 */
#include "plugin_runtime.h"
#include "protocol.h"

#include "plugin_manager/plugin_abi.h"

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
    return ef800_protocol_elta3_parse(response, result);
}

static int32_t input(void *instance, uint32_t value)
{
    if (instance != &ef800_runtime.public_instance) return PM_EINVAL;
    if (value == EF800_MEASUREMENT_INPUT_UP) return send("S12\r\n");
    if (value == EF800_MEASUREMENT_INPUT_DOWN) return send("S11\r\n");
    if (value == EF800_MEASUREMENT_INPUT_ENTER) return send("S14\r\n");
    return PM_ENOTSUP;
}

const struct ef800_measurement_api ef800_plugin_api = {
    EF800_MEASUREMENT_API_VERSION, sizeof(struct ef800_measurement_api),
    defaults, measure, input, {0u}
};
