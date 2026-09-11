/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_HWDEBUG_PERIPHERAL_TESTS_H
#define EF800_HWDEBUG_PERIPHERAL_TESTS_H

#include <stdbool.h>
#include <stdint.h>

void hwdebug_keyboard_test(const char *port_name);
void hwdebug_keyboard_interactive(void);
void hwdebug_display_test(void);
void hwdebug_display_interactive(void);
void hwdebug_display_wr_test(uint32_t count);
void hwdebug_display_status(void);
void hwdebug_uart_test(const char *port_name);
void hwdebug_lpuart_test(void);
void hwdebug_lpuart_host_test(void);
void hwdebug_lpuart_bitbang_test(void);
void hwdebug_storage_test(void);
bool hwdebug_interactive_pending(void);
void hwdebug_interactive_reply(const char *reply);

#endif
