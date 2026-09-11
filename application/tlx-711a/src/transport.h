/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TLX711A_TRANSPORT_H
#define TLX711A_TRANSPORT_H

#include <stdint.h>

int transport_init(void);
void transport_send_frame(const uint8_t *frame, uint16_t length);
void transport_send_info(const char *message);

#endif
