/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_HWDEBUG_PERIPHERAL_TESTS_H
#define EF800_HWDEBUG_PERIPHERAL_TESTS_H

#include <stdbool.h>
#include <stdint.h>

void hwdebug_keyboard_test(const char *port_name);
void hwdebug_keyboard_baud_test(void);
void hwdebug_keyboard_version(void);
void hwdebug_keyboard_version_raw(void);
void hwdebug_keyboard_id_raw(void);
int hwdebug_keyboard_reset_release(void);
void hwdebug_keyboard_interactive(void);
void hwdebug_display_test(void);
void hwdebug_display_interactive(void);
void hwdebug_display_wr_test(uint32_t count);
void hwdebug_display_status(void);
void hwdebug_uart_test(const char *port_name);
void hwdebug_instrument_uart_test(const char *instrument, const char *uart_name);
void hwdebug_lpuart_test(void);
void hwdebug_lpuart_host_test(void);
void hwdebug_lpuart_bitbang_test(void);
void hwdebug_storage_test(void);
void hwdebug_storage_format_layout(void);
void hwdebug_storage_format_volumes(void);
void hwdebug_usb_cdc_test(void);
void hwdebug_keyboard_update(const char *path);
void hwdebug_keyboard_receive(void);
void hwdebug_keyboard_bootloader_probe(void);
int hwdebug_storage_prepare_update(void);
bool hwdebug_interactive_pending(void);
void hwdebug_interactive_reply(const char *reply);

#endif
