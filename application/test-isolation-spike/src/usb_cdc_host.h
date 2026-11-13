/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Host-side USB CDC transport for the isolation spike. */
#ifndef TEST_ISOLATION_SPIKE_USB_CDC_HOST_H
#define TEST_ISOLATION_SPIKE_USB_CDC_HOST_H

#include <stddef.h>
#include <stdint.h>

/** @brief Initialize USB host and wait for a CDC ACM peripheral.
 * @return 0 on setup; negative errno if host initialization fails.
 */
int spike_usb_cdc_host_init(void);

/** @brief Read one byte from the connected CDC ACM device.
 * @param byte Receives the byte read.
 * @return 0 on read; negative errno when no data/device is available.
 */
int spike_usb_cdc_host_read(uint8_t *byte);

/** @brief Write a byte range to the connected CDC ACM device.
 * @param data Bytes to transmit.
 * @param length Byte count.
 * @return None; transport errors are reported by the host console.
 */
void spike_usb_cdc_host_write(const uint8_t *data, size_t length);

#endif /* TEST_ISOLATION_SPIKE_USB_CDC_HOST_H */
