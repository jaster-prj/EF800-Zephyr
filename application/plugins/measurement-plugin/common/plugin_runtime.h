/* SPDX-License-Identifier: Apache-2.0 */
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

int pm_plugin_create(const struct pm_service_table *services, void **instance);
int pm_plugin_destroy(void *instance);
void ef800_measurement_display_init(struct ef800_measurement_display *display,
    const char *title, uint32_t actions);
int ef800_measurement_display_text(char *output, uint32_t capacity,
    const char *value);
int ef800_measurement_display_number(char *output, uint32_t capacity,
    const char *label, int32_t value);
extern struct ef800_plugin_runtime ef800_runtime;
extern const struct ef800_measurement_api ef800_plugin_api;

#endif
