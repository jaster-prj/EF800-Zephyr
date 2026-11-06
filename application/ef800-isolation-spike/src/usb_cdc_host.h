/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_ISOLATION_SPIKE_USB_CDC_HOST_H
#define EF800_ISOLATION_SPIKE_USB_CDC_HOST_H

#include <stddef.h>
#include <stdint.h>

int spike_usb_cdc_host_init(void);
int spike_usb_cdc_host_read(uint8_t *byte);
void spike_usb_cdc_host_write(const uint8_t *data, size_t length);

#endif /* EF800_ISOLATION_SPIKE_USB_CDC_HOST_H */
