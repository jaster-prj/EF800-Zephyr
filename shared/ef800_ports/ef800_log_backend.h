/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Route Zephyr log records to the EF800 console transport. */
#ifndef EF800_LOG_BACKEND_H
#define EF800_LOG_BACKEND_H

#include <stdint.h>

/** @brief Select the application debug output port for subsequent log records.
 * @param port_id Physical port identifier.
 * @return 0 on selection; negative errno if the port is invalid/unavailable.
 */
int ef800_log_backend_set_target(uint8_t port_id);

/** @brief Get the selected log output port.
 * @return Port ID, or the no-target sentinel when not selected.
 */
uint8_t ef800_log_backend_get_target(void);

#endif /* EF800_LOG_BACKEND_H */
