/* SPDX-License-Identifier: Apache-2.0 */
#include "plugin_runtime.h"

#include "ef800_plugin_ids.h"

struct ef800_plugin_runtime ef800_runtime;

static int uuid_equal(const pm_uuid_t *left, const pm_uuid_t *right)
{
    uint32_t index;
    for (index = 0u; index < PM_UUID_SIZE; ++index)
        if (left->bytes[index] != right->bytes[index]) return 0;
    return 1;
}

const struct ef800_port_api *ef800_find_port(const struct pm_service_table *services)
{
    size_t index;
    if (services == 0 || services->items == 0) return 0;
    for (index = 0u; index < services->count; ++index) {
        const struct pm_service *service = &services->items[index];
        if (uuid_equal(&service->service_id, &EF800_PORT_SERVICE_ID) &&
            service->version >= EF800_PORT_SERVICE_VERSION && service->vtable != 0) {
            const struct ef800_port_api *port = service->vtable;
            if (port->abi_version == EF800_PORT_API_VERSION &&
                port->struct_size >= sizeof(struct ef800_port_api) &&
                port->write != 0 && port->read != 0) return port;
        }
    }
    return 0;
}

int pm_plugin_create(const struct pm_service_table *services, void **instance)
{
    const struct ef800_port_api *port;
    if (instance == 0) return PM_EINVAL;
    port = ef800_find_port(services);
    if (port == 0) return PM_ENOENT;
    if (ef800_runtime.active != 0u) return PM_EBUSY;
    ef800_runtime.public_instance.api = &ef800_plugin_api;
    ef800_runtime.port = port;
    ef800_runtime.active = 1u;
    *instance = &ef800_runtime.public_instance;
    return PM_OK;
}

int pm_plugin_destroy(void *instance)
{
    if (instance != &ef800_runtime.public_instance || ef800_runtime.active == 0u)
        return PM_EINVAL;
    ef800_runtime.public_instance.api = 0;
    ef800_runtime.port = 0;
    ef800_runtime.active = 0u;
    return PM_OK;
}
