/* SPDX-License-Identifier: Apache-2.0 */

#ifndef EF800_HWDEBUG_FLASH_H
#define EF800_HWDEBUG_FLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct device;

const struct device *hwdebug_console_device(void);
void hwdebug_console_puts(const char *text);
void hwdebug_console_printf(const char *format, ...);
void hwdebug_console_hexdump(uint32_t offset, const uint8_t *data, size_t length);

int hwdebug_flash_watchdog_init(void);
void hwdebug_flash_watchdog_feed(void);
void hwdebug_flash_watchdog_test(void);
void hwdebug_flash_bitbang(const char *mode);
void hwdebug_flash_info(void);
void hwdebug_flash_probe(void);
void hwdebug_flash_jedec(void);
void hwdebug_flash_sfdp(uint32_t offset, uint32_t length);
void hwdebug_flash_read(uint32_t offset, uint32_t length);
int hwdebug_flash_recover(bool verbose);
void hwdebug_flash_test_group(const char *name);
void hwdebug_flash_test_all(void);

#endif
