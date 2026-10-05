/* SPDX-License-Identifier: Apache-2.0 */
#include "plugin_runtime.h"
#include "protocol.h"

#include "plugin_manager/plugin_abi.h"

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
    return ef800_protocol_fts3xx_parse(response, result);
}

static int32_t input(void *instance, uint32_t value)
{
    (void)value;
    return instance == &ef800_runtime.public_instance ? PM_ENOTSUP : PM_EINVAL;
}

const struct ef800_measurement_api ef800_plugin_api = {
    EF800_MEASUREMENT_API_VERSION, sizeof(struct ef800_measurement_api),
    defaults, measure, input, {0u}
};
