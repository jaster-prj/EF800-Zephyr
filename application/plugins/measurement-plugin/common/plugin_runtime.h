/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Supply common lifecycle and bounded display helpers to plugins. */
#ifndef EF800_PLUGIN_RUNTIME_H
#define EF800_PLUGIN_RUNTIME_H

#include "ef800_measurement_plugin.h"
#include "ef800_port_service.h"
#include "plugin_manager/plugin_abi.h"

struct ef800_plugin_runtime {
    struct ef800_measurement_instance public_instance;
    ef800_port_binding binding;
    uint32_t active;
    uint32_t reserved[3];
};

/** @brief Create the common plugin runtime and bind its worker services.
 * @param services Host service table supplied by the plugin manager.
 * @param instance Receives the opaque public plugin instance.
 * @return 0 on creation; negative errno when required services are absent.
 */
int pm_plugin_create(const struct pm_service_table *services, void **instance);

/** @brief Destroy the common plugin runtime and release its resources.
 * @param instance Instance returned by pm_plugin_create.
 * @return 0 on destruction; negative errno for invalid instance/state.
 */
int pm_plugin_destroy(void *instance);

/** @brief Send a bounded diagnostic message through the worker log service.
 * @param message Message bytes; a NUL terminator is not required.
 * @param length Number of message bytes.
 * @return 0 on success; negative errno or service status on failure.
 */
int32_t ef800_plugin_runtime_log(const char *message, uint32_t length);

/** @brief Initialize a bounded display model with title and action mask.
 * @param display Display model to clear and initialize.
 * @param title Display title.
 * @param actions Supported action bitmask.
 * @return None.
 */
void ef800_measurement_display_init(struct ef800_measurement_display *display,
    const char *title, uint32_t actions);

/** @brief Copy bounded text into a display row buffer.
 * @param output Destination buffer.
 * @param capacity Destination capacity.
 * @param value NUL-terminated source text.
 * @return 0 on copy; negative errno if invalid or text does not fit.
 */
int ef800_measurement_display_text(char *output, uint32_t capacity,
    const char *value);

/** @brief Format a labeled signed number into a display row buffer.
 * @param output Destination buffer.
 * @param capacity Destination capacity.
 * @param label Prefix label.
 * @param value Signed value to format.
 * @return 0 on format; negative errno if invalid or formatted text does not fit.
 */
int ef800_measurement_display_number(char *output, uint32_t capacity,
    const char *label, int32_t value);
extern struct ef800_plugin_runtime ef800_runtime;
extern const struct ef800_measurement_api ef800_plugin_api;

#endif
