/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_NUCLEO_HOST_POLICY_H
#define EF800_NUCLEO_HOST_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum host_menu_state {
    HOST_MENU_UNASSIGNED,
    HOST_MENU_MISSING,
    HOST_MENU_UNAVAILABLE,
    HOST_MENU_READY,
};

struct host_binding {
    uint32_t plugin_id;
    uint8_t port_id;
    bool route_ready;
    bool package_available;
};

/* The reference host exposes USART1 on port 2 with optional RTS/CTS. */
bool host_port_compatible(uint8_t port_id, bool needs_flow_control);
int host_assignment_check(uint8_t port_id, uint8_t other_port_id);
enum host_menu_state host_measurement_state(const struct host_binding *binding);
bool host_action_allowed(const struct host_binding *binding, uint32_t selected_id);

#endif
