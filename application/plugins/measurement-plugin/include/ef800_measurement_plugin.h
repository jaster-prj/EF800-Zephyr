/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Define the measurement plugin lifecycle, UI, and result ABI. */
#ifndef EF800_MEASUREMENT_PLUGIN_H
#define EF800_MEASUREMENT_PLUGIN_H

#include <stdint.h>

#define EF800_MEASUREMENT_API_VERSION 3u
#define EF800_MEASUREMENT_UI_API_VERSION 1u
#define EF800_MEASUREMENT_SCREEN_ROWS 6u
#define EF800_MEASUREMENT_SCREEN_COLUMNS 40u
#define EF800_MEASUREMENT_MAX_TITLE 31u

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

#ifndef EF800_MEASUREMENT_ACTION_MEASURE
#define EF800_MEASUREMENT_ACTION_MEASURE (1u << 0)
#define EF800_MEASUREMENT_ACTION_UP (1u << 1)
#define EF800_MEASUREMENT_ACTION_DOWN (1u << 2)
#define EF800_MEASUREMENT_ACTION_ENTER (1u << 3)
#define EF800_MEASUREMENT_ACTION_CODE (1u << 4)
#define EF800_MEASUREMENT_ACTION_POINT (1u << 5)
#define EF800_MEASUREMENT_ACTION_STAFF_HEIGHT (1u << 6)
#define EF800_MEASUREMENT_ACTION_SAVE (1u << 7)
#define EF800_MEASUREMENT_ACTION_DISCARD (1u << 8)
#define EF800_MEASUREMENT_ACTION_FIX (1u << 9)
#endif

enum ef800_measurement_key_code {
    EF800_MEASUREMENT_KEY_UP = 1,
    EF800_MEASUREMENT_KEY_DOWN,
    EF800_MEASUREMENT_KEY_LEFT,
    EF800_MEASUREMENT_KEY_RIGHT,
    EF800_MEASUREMENT_KEY_ENTER,
    EF800_MEASUREMENT_KEY_BACK,
    EF800_MEASUREMENT_KEY_CHARACTER,
};

struct ef800_measurement_key_event {
    uint32_t struct_size;
    uint32_t code;
    uint32_t modifiers;
    uint8_t character;
    uint8_t reserved[3];
};

enum ef800_measurement_key_result {
    EF800_MEASUREMENT_KEY_NOT_CONSUMED = 0,
    EF800_MEASUREMENT_KEY_CONSUMED = 1,
    EF800_MEASUREMENT_KEY_REQUEST_BACK = 2,
    EF800_MEASUREMENT_KEY_UNSUPPORTED = 3,
    EF800_MEASUREMENT_KEY_ERROR = 4,
};

struct ef800_measurement_display {
    uint32_t struct_size;
    char title[EF800_MEASUREMENT_MAX_TITLE + 1u];
    char rows[EF800_MEASUREMENT_SCREEN_ROWS][EF800_MEASUREMENT_SCREEN_COLUMNS + 1u];
    char footer[EF800_MEASUREMENT_SCREEN_COLUMNS + 1u];
    uint32_t actions;
    uint8_t selected_action;
    uint8_t row_count;
    uint8_t reserved[2];
};

/** @brief Validate a plugin display model's sizes and selected row bounds.
 * @param display Display model supplied by a plugin.
 * @return 0 when valid; negative errno when the model exceeds ABI limits.
 */
int ef800_measurement_display_validate(
    const struct ef800_measurement_display *display);

enum ef800_measurement_session_state {
    EF800_MEASUREMENT_SESSION_CLOSED = 0,
    EF800_MEASUREMENT_SESSION_ACTIVE = 1,
    EF800_MEASUREMENT_SESSION_FAILED = 2,
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

struct ef800_measurement_context {
    uint32_t struct_size;
    int32_t point_number;
    char code[49];
};

struct ef800_measurement_api {
    uint32_t abi_version;
    uint32_t struct_size;
    /* Fill device serial defaults; return 0 or a negative error code. */
    int32_t (*get_serial_defaults)(void *instance,
        struct ef800_serial_defaults *defaults);
    /* Perform one observation and fill result fields. */
    int32_t (*measure)(void *instance, struct ef800_measurement_result *result);
    /* Handle a legacy directional action. */
    int32_t (*handle_input)(void *instance, uint32_t input);
    uint32_t ui_abi_version;
    uint32_t ui_struct_size;
    uint32_t actions;
    /* Fill a bounded UI model and handle normalized key events. */
    int32_t (*render)(void *instance, struct ef800_measurement_display *display);
    int32_t (*handle_key)(void *instance,
        const struct ef800_measurement_key_event *event,
        uint32_t *result);
    int32_t (*set_context)(void *instance,
        const struct ef800_measurement_context *context);
    /* Return a bounded workspace record and receive its commit result. */
    int32_t (*get_workspace_record)(void *instance, char *buffer,
        uint32_t capacity, uint32_t *length);
    int32_t (*workspace_record_result)(void *instance, int32_t status);
    uint32_t reserved[1];
};

/* pm_plugin_create returns a pointer to this object as its opaque instance. */
struct ef800_measurement_instance {
    const struct ef800_measurement_api *api;
};

#endif
