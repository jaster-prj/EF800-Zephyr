/* SPDX-License-Identifier: Apache-2.0 */
#include "host_policy.h"

#include <errno.h>

bool host_port_compatible(uint8_t port_id, bool needs_flow_control)
{
    (void)needs_flow_control;
    return port_id == 2u;
}

int host_assignment_check(uint8_t port_id, uint8_t other_port_id)
{
    if (port_id != 0u && !host_port_compatible(port_id, false)) return -EINVAL;
    return port_id != 0u && port_id == other_port_id ? -EBUSY : 0;
}

enum host_menu_state host_measurement_state(const struct host_binding *binding)
{
    if (binding == NULL || binding->port_id == 0u) {
        return HOST_MENU_UNASSIGNED;
    }
    if (!binding->package_available) {
        return HOST_MENU_MISSING;
    }
    return binding->route_ready ? HOST_MENU_READY : HOST_MENU_UNAVAILABLE;
}

bool host_action_allowed(const struct host_binding *binding, uint32_t selected_id)
{
    return binding != NULL && binding->plugin_id == selected_id &&
           host_measurement_state(binding) == HOST_MENU_READY;
}
