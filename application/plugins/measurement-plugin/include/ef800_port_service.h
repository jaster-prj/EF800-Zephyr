/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_PORT_SERVICE_H
#define EF800_PORT_SERVICE_H

/*
 * Isolated Port SDK surface. Bindings contain only a mailbox address in the
 * worker's own user mapping and scalar generation/capability values. There is
 * no host context pointer or host operation callback in this interface.
 */
#include "ef800_plugin_worker_sdk.h"

typedef struct ef800_plugin_worker_binding ef800_port_binding;

#endif /* EF800_PORT_SERVICE_H */
