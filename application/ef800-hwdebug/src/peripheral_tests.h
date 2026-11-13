/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Hardware-debug commands for keyboard, display, serial, storage, and USB.
 */
#ifndef EF800_HWDEBUG_PERIPHERAL_TESTS_H
#define EF800_HWDEBUG_PERIPHERAL_TESTS_H

#include <stdbool.h>
#include <stdint.h>

/** @brief Run the keyboard protocol test on a selected UART port.
 * @param port_name UART/port selector string.
 * @return None; prints test results.
 */
void hwdebug_keyboard_test(const char *port_name);

/** @brief Exercise the optional keyboard baud-rate switch protocol.
 * @return None; prints test results.
 */
void hwdebug_keyboard_baud_test(void);

/** @brief Query and print the decoded keyboard firmware version.
 * @return None.
 */
void hwdebug_keyboard_version(void);

/** @brief Query and print the raw keyboard version response bytes.
 * @return None.
 */
void hwdebug_keyboard_version_raw(void);

/** @brief Query and print the raw keyboard identifier response bytes.
 * @return None.
 */
void hwdebug_keyboard_id_raw(void);

/** @brief Release the keyboard reset line.
 * @return 0 on success; negative errno on GPIO failure.
 */
int hwdebug_keyboard_reset_release(void);

/** @brief Enter interactive keyboard diagnostic mode.
 * @return None.
 */
void hwdebug_keyboard_interactive(void);

/** @brief Run the display pixel/timing diagnostic.
 * @return None.
 */
void hwdebug_display_test(void);

/** @brief Enter interactive display command mode.
 * @return None.
 */
void hwdebug_display_interactive(void);

/** @brief Perform a bounded display write/read test.
 * @param count Number of iterations to run.
 * @return None.
 */
void hwdebug_display_wr_test(uint32_t count);

/** @brief Print display initialization and status information.
 * @return None.
 */
void hwdebug_display_status(void);

/** @brief Run a serial loopback test on a named UART.
 * @param port_name UART selector.
 * @return None.
 */
void hwdebug_uart_test(const char *port_name);

/** @brief Test the instrument UART selected by name.
 * @param instrument Instrument label used in output.
 * @param uart_name UART selector.
 * @return None.
 */
void hwdebug_instrument_uart_test(const char *instrument, const char *uart_name);

/** @brief Test the board's LPUART connection.
 * @return None.
 */
void hwdebug_lpuart_test(void);

/** @brief Test the LPUART using the host-side path.
 * @return None.
 */
void hwdebug_lpuart_host_test(void);

/** @brief Test LPUART signaling using software bit-banging.
 * @return None.
 */
void hwdebug_lpuart_bitbang_test(void);

/** @brief Inspect and exercise the configured storage device.
 * @return None.
 */
void hwdebug_storage_test(void);

/** @brief Print the fixed storage partition layout.
 * @return None.
 */
void hwdebug_storage_format_layout(void);

/** @brief Print detected filesystem volume layout and status.
 * @return None.
 */
void hwdebug_storage_format_volumes(void);

/** @brief Run the USB CDC device diagnostic.
 * @return None.
 */
void hwdebug_usb_cdc_test(void);

/** @brief Transfer a keyboard firmware package from the supplied path.
 * @param path Package path accessible to the debug image.
 * @return None; reports transfer outcome on the console.
 */
void hwdebug_keyboard_update(const char *path);

/** @brief Receive and report keyboard UART traffic.
 * @return None.
 */
void hwdebug_keyboard_receive(void);

/** @brief Probe the keyboard bootloader entry handshake.
 * @return None.
 */
void hwdebug_keyboard_bootloader_probe(void);

/** @brief Prepare shared storage for a keyboard update operation.
 * @return 0 when prepared; negative errno if storage ownership/setup fails.
 */
int hwdebug_storage_prepare_update(void);

/** @brief Check whether an interactive diagnostic has pending input.
 * @return True when input is pending.
 */
bool hwdebug_interactive_pending(void);

/** @brief Send a response to the active interactive diagnostic.
 * @param reply NUL-terminated response text.
 * @return None.
 */
void hwdebug_interactive_reply(const char *reply);

#endif
