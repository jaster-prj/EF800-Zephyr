/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_MEASUREMENT_PROTOCOL_H
#define EF800_MEASUREMENT_PROTOCOL_H

#include "ef800_measurement_plugin.h"
#include "ef800_port_service.h"

int32_t ef800_protocol_write(ef800_port_binding *port,
                             const uint8_t *data, uint32_t length);
int32_t ef800_protocol_read_line(ef800_port_binding *port,
                                 char *buffer, uint32_t capacity);
int32_t ef800_protocol_parse_decimal(const char *text, uint32_t scale,
                                     int32_t *value);
uint32_t ef800_protocol_split(char *text, char **tokens, uint32_t capacity);
uint32_t ef800_protocol_length(const char *text);
int32_t ef800_protocol_elta3_parse(char *line,
                                   struct ef800_measurement_result *result);
int32_t ef800_protocol_fts3xx_parse(char *line,
                                    struct ef800_measurement_result *result);

#endif
