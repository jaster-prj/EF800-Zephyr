/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_LOG_BACKEND_H
#define EF800_LOG_BACKEND_H

#include <stdint.h>

/* Select the application debug output port for subsequent log messages. */
int ef800_log_backend_set_target(uint8_t port_id);
uint8_t ef800_log_backend_get_target(void);

#endif /* EF800_LOG_BACKEND_H */
