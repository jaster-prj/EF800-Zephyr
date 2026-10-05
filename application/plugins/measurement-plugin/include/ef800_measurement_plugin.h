/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_MEASUREMENT_PLUGIN_H
#define EF800_MEASUREMENT_PLUGIN_H

#include <stdint.h>

#define EF800_MEASUREMENT_API_VERSION 1u

enum ef800_serial_parity {
    EF800_SERIAL_PARITY_NONE = 0,
    EF800_SERIAL_PARITY_ODD = 1,
    EF800_SERIAL_PARITY_EVEN = 2,
};

enum ef800_serial_flow_control {
    EF800_SERIAL_FLOW_NONE = 0,
    EF800_SERIAL_FLOW_RTS_CTS = 1,
};

enum ef800_measurement_input {
    EF800_MEASUREMENT_INPUT_UP = 1,
    EF800_MEASUREMENT_INPUT_DOWN = 2,
    EF800_MEASUREMENT_INPUT_ENTER = 3,
};

struct ef800_serial_defaults {
    uint32_t struct_size;
    uint32_t baudrate;
    uint32_t data_bits;
    uint32_t parity;
    uint32_t stop_bits;
    uint32_t flow_control;
    uint32_t reserved[4];
};

struct ef800_measurement_result {
    uint32_t struct_size;
    int32_t distance_mm;
    int32_t horizontal_gon_1e4;
    int32_t vertical_gon_1e4;
    uint8_t control;
    uint8_t reserved[3];
    uint32_t flags;
    uint32_t reserved_words[3];
};

struct ef800_measurement_api {
    uint32_t abi_version;
    uint32_t struct_size;
    int32_t (*get_serial_defaults)(void *instance,
        struct ef800_serial_defaults *defaults);
    int32_t (*measure)(void *instance, struct ef800_measurement_result *result);
    int32_t (*handle_input)(void *instance, uint32_t input);
    uint32_t reserved[4];
};

/* pm_plugin_create returns a pointer to this object as its opaque instance. */
struct ef800_measurement_instance {
    const struct ef800_measurement_api *api;
};

#endif
