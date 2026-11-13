/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Hardware-debug console and QSPI flash diagnostics/recovery commands.
 */

#ifndef EF800_HWDEBUG_FLASH_H
#define EF800_HWDEBUG_FLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct device;

/** @brief Get the configured hardware-debug console device.
 * @return Console device, or NULL when no device is configured.
 */
const struct device *hwdebug_console_device(void);

/** @brief Write a NUL-terminated string to the debug console.
 * @param text Text to send; NULL is ignored.
 * @return None.
 */
void hwdebug_console_puts(const char *text);

/** @brief Format and write a message to the debug console.
 * @param format printf-style format string.
 * @param ... Values referenced by format.
 * @return None.
 */
void hwdebug_console_printf(const char *format, ...);

/** @brief Print a byte buffer as a hexadecimal dump.
 * @param offset Address/offset label for the first byte.
 * @param data Bytes to display.
 * @param length Number of bytes in data.
 * @return None.
 */
void hwdebug_console_hexdump(uint32_t offset, const uint8_t *data, size_t length);

/** @brief Initialize watchdog handling for long-running flash diagnostics.
 * @return 0 on success; negative errno if setup fails.
 */
int hwdebug_flash_watchdog_init(void);

/** @brief Feed the watchdog during a diagnostic operation.
 * @return None.
 */
void hwdebug_flash_watchdog_feed(void);

/** @brief Run the watchdog feed/reset diagnostic.
 * @return None.
 */
void hwdebug_flash_watchdog_test(void);

/** @brief Run the selected software-driven flash bus mode.
 * @param mode Requested bus mode string.
 * @return None.
 */
void hwdebug_flash_bitbang(const char *mode);

/** @brief Print detected flash geometry and identification.
 * @return None.
 */
void hwdebug_flash_info(void);

/** @brief Probe the flash device and report whether it responds.
 * @return None.
 */
void hwdebug_flash_probe(void);

/** @brief Read and print the flash JEDEC identification bytes.
 * @return None.
 */
void hwdebug_flash_jedec(void);

/** @brief Print a bounded SFDP data range.
 * @param offset SFDP byte offset.
 * @param length Number of bytes to inspect.
 * @return None.
 */
void hwdebug_flash_sfdp(uint32_t offset, uint32_t length);

/** @brief Read and display a range using the selected flash API.
 * @param offset Flash byte offset.
 * @param length Number of bytes to read.
 * @return None.
 */
void hwdebug_flash_read(uint32_t offset, uint32_t length);

/** @brief Read and display a range using software bit-banged access.
 * @param offset Flash byte offset.
 * @param length Number of bytes to read.
 * @return None.
 */
void hwdebug_flash_bitbang_read(uint32_t offset, uint32_t length);

/** @brief Read and display a range through the Zephyr flash API.
 * @param offset Flash byte offset.
 * @param length Number of bytes to read.
 * @return None.
 */
void hwdebug_flash_api_read(uint32_t offset, uint32_t length);

/** @brief Run the explicitly bounded flash program/erase diagnostic.
 * @return None.
 */
void hwdebug_flash_program_test(void);

/** @brief Recover the configured flash state and optionally print progress.
 * @param verbose True to report each recovery step.
 * @return 0 on recovery; negative errno if recovery fails.
 */
int hwdebug_flash_recover(bool verbose);

/** @brief Run one named flash diagnostic group.
 * @param name Group name selected by the caller.
 * @return None.
 */
void hwdebug_flash_test_group(const char *name);

/** @brief Run the complete flash diagnostic suite.
 * @return None.
 */
void hwdebug_flash_test_all(void);

#endif
