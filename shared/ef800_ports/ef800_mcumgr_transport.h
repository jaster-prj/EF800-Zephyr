/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Manage USB/serial transport for MCUmgr management requests. */
#ifndef EF800_MCUMGR_TRANSPORT_H
#define EF800_MCUMGR_TRANSPORT_H

/** @brief Initialize the configured MCUmgr transport backend.
 * @return 0 on setup; negative errno if transport is unavailable.
 */
int ef800_mcumgr_transport_init(void);

/** @brief Stop MCUmgr transport and release its active resources.
 * @return None.
 */
void ef800_mcumgr_transport_stop(void);

#endif /* EF800_MCUMGR_TRANSPORT_H */
