/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Provide bounded port I/O and instrument response parsers to plugins. */
#ifndef EF800_MEASUREMENT_PROTOCOL_H
#define EF800_MEASUREMENT_PROTOCOL_H

#include "ef800_measurement_plugin.h"
#include "ef800_port_service.h"

/** @brief Write bytes through an isolated plugin port capability.
 * @param port Worker port binding.
 * @param data Bytes to write.
 * @param length Number of bytes.
 * @return Bytes written or negative errno.
 */
int32_t ef800_protocol_write(ef800_port_binding *port,
                             const uint8_t *data, uint32_t length);

/** @brief Read one bounded line from a plugin port.
 * @param port Worker port binding.
 * @param buffer Receives line bytes and terminator.
 * @param capacity Buffer capacity.
 * @return Line length or negative errno.
 */
int32_t ef800_protocol_read_line(ef800_port_binding *port,
                                 char *buffer, uint32_t capacity);

/** @brief Parse decimal text and scale it into a signed integer.
 * @param text NUL-terminated decimal value.
 * @param scale Fixed-point scale multiplier.
 * @param value Receives scaled result.
 * @return 0 on parse; negative errno for malformed/overflowing text.
 */
int32_t ef800_protocol_parse_decimal(const char *text, uint32_t scale,
                                     int32_t *value);

/** @brief Split mutable text on protocol delimiters into bounded tokens.
 * @param text Text to modify and split.
 * @param tokens Receives pointers into text.
 * @param capacity Maximum token pointers.
 * @return Number of tokens stored.
 */
uint32_t ef800_protocol_split(char *text, char **tokens, uint32_t capacity);

/** @brief Return the length of a NUL-terminated protocol string.
 * @param text Input string.
 * @return String length, or zero for NULL.
 */
uint32_t ef800_protocol_length(const char *text);

/** @brief Parse an ELTA3 response line into the public result structure.
 * @param line Mutable response line to tokenize.
 * @param result Receives parsed measurement fields.
 * @return 0 on parse; negative errno for malformed response.
 */
int32_t ef800_protocol_elta3_parse(char *line,
                                   struct ef800_measurement_result *result);

/** @brief Parse an FTS3xx response line into the public result structure.
 * @param line Mutable response line to tokenize.
 * @param result Receives parsed measurement fields.
 * @return 0 on parse; negative errno for malformed response.
 */
int32_t ef800_protocol_fts3xx_parse(char *line,
                                    struct ef800_measurement_result *result);

#endif
