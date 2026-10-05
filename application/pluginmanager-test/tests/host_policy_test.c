/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <string.h>

#include "calculation_plugin.h"
#include "host_policy.h"

int main(void)
{
    struct host_binding binding = { .plugin_id = 2u, .port_id = 0u };
    struct pm_service_table empty_services = { 0 };
    struct ef800_calculation_instance *instance;
    char result[32];
    size_t length;
    void *opaque = NULL;

    assert(host_measurement_state(&binding) == HOST_MENU_UNASSIGNED);
    binding.port_id = 2u;
    assert(host_measurement_state(&binding) == HOST_MENU_MISSING);
    binding.package_available = true;
    assert(host_measurement_state(&binding) == HOST_MENU_UNAVAILABLE);
    binding.route_ready = true;
    assert(host_measurement_state(&binding) == HOST_MENU_READY);
    /* Measure and directional actions share this one selected binding. */
    assert(host_action_allowed(&binding, 2u));
    assert(!host_action_allowed(&binding, 3u));
    assert(host_port_compatible(2u, true));
    assert(!host_port_compatible(1u, false));
    assert(host_assignment_check(2u, 0u) == 0);
    assert(host_assignment_check(2u, 2u) == -EBUSY);
    assert(host_assignment_check(1u, 0u) == -EINVAL);

    assert(ef800_offset_calculation_descriptor.create(&empty_services, &opaque) == 0);
    instance = opaque;
    assert(instance->api->transform(instance, "100\n-20\n", 8u, 5,
                                    result, sizeof(result), &length) == 0);
    assert(length == 8u && memcmp(result, "105\n-15\n", length) == 0);
    assert(instance->api->transform(instance, "100\n", 4u, 5,
                                    result, 4u, &length) == -EFBIG);
    assert(instance->api->transform(instance, "bad", 3u, 5,
                                    result, sizeof(result), &length) == -EINVAL);
    assert(instance->api->transform(instance, "2147483647", 10u, 1,
                                    result, sizeof(result), &length) == -ERANGE);
    assert(ef800_offset_calculation_descriptor.destroy(instance) == 0);
    return 0;
}
