/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Select and manage the active Shell transport. */
#ifndef EF800_SHELL_TRANSPORT_H
#define EF800_SHELL_TRANSPORT_H

#include <stdbool.h>

/** @brief Initialize Shell transport routing for the active console.
 * @return 0 on setup; negative errno if transport initialization fails.
 */
int ef800_shell_init(void);

/** @brief Enable or disable binary mode on the shell transport.
 * @param enabled True to suppress text shell processing for binary traffic.
 * @return None.
 */
void ef800_shell_set_binary_mode(bool enabled);

#endif /* EF800_SHELL_TRANSPORT_H */
