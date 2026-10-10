/* SPDX-License-Identifier: Apache-2.0 */
#include "protocol.h"

#include "plugin_manager/plugin_abi.h"

#define RESPONSE_CAPACITY 128u

static uint32_t divide_u32(uint32_t dividend, uint32_t divisor);

uint32_t ef800_protocol_length(const char *text)
{
    uint32_t length = 0u;
    if (text == 0) return 0u;
    while (text[length] != '\0') ++length;
    return length;
}

int32_t ef800_protocol_write(const struct ef800_port_api *port,
                             const uint8_t *data, uint32_t length)
{
    int32_t written;
    if (port == 0 || port->write == 0 || (data == 0 && length != 0u))
        return PM_EINVAL;
    written = port->write(port->context, data, length);
    return written < 0 ? written : ((uint32_t)written == length ? PM_OK : PM_EIO);
}

int32_t ef800_protocol_read_line(const struct ef800_port_api *port,
                                 char *buffer, uint32_t capacity)
{
    uint32_t length = 0u;
    if (port == 0 || port->read == 0 || buffer == 0 || capacity < 2u)
        return PM_EINVAL;
    while (length + 1u < capacity) {
        uint8_t byte;
        int32_t count = port->read(port->context, &byte, 1u, 1000);
        if (count < 0) return count;
        if (count == 0) return -110;
        if (count != 1) return PM_EIO;
        buffer[length++] = (char)byte;
        if (byte == (uint8_t)'\n') {
            buffer[length] = '\0';
            return (int32_t)length;
        }
    }
    return PM_EOVERFLOW;
}

int32_t ef800_protocol_parse_decimal(const char *text, uint32_t scale,
                                     int32_t *value)
{
    uint32_t whole = 0u, fraction = 0u, divisor = 1u;
    int32_t sign = 1;
    uint8_t decimal = 0u, digit = 0u;
    if (text == 0 || value == 0 || scale == 0u || *text == '\0') return PM_EINVAL;
    if (*text == '-' || *text == '+') { sign = *text == '-' ? -1 : 1; ++text; }
    while (*text != '\0') {
        uint32_t number;
        if (*text == ';') { ++text; continue; }
        if (*text == '.') {
            if (decimal != 0u) return PM_EINVAL;
            decimal = 1u; ++text; continue;
        }
        if (*text < '0' || *text > '9') return PM_EINVAL;
        digit = 1u; number = (uint32_t)(*text - '0');
        if (decimal == 0u) {
            if (whole > 214748364u) return PM_EOVERFLOW;
            whole = whole * 10u + number;
        } else if (divisor < scale) {
            fraction = fraction * 10u + number;
            divisor *= 10u;
        } else if (number != 0u) return PM_EINVAL;
        ++text;
    }
    if (digit == 0u) return PM_EINVAL;
    while (divisor < scale) { fraction *= 10u; divisor *= 10u; }
    if (whole > divide_u32(2147483647u, scale) ||
        whole * scale > 2147483647u - fraction) return PM_EOVERFLOW;
    *value = sign * (int32_t)(whole * scale + fraction);
    return PM_OK;
}

uint32_t ef800_protocol_split(char *text, char **tokens, uint32_t capacity)
{
    uint32_t count = 0u;
    while (*text != '\0' && count < capacity) {
        while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') ++text;
        if (*text == '\0') break;
        tokens[count++] = text;
        while (*text != '\0' && *text != ' ' && *text != '\t' &&
               *text != '\r' && *text != '\n') ++text;
        if (*text != '\0') *text++ = '\0';
    }
    return count;
}

static uint32_t divide_u32(uint32_t dividend, uint32_t divisor)
{
    uint32_t quotient = 0u, remainder = 0u;
    int32_t bit;
    for (bit = 31; bit >= 0; --bit) {
        remainder = (remainder << 1u) | ((dividend >> (uint32_t)bit) & 1u);
        if (remainder >= divisor) {
            remainder -= divisor;
            quotient |= 1u << (uint32_t)bit;
        }
    }
    return quotient;
}

static int32_t parse_fts_angle(const char *text, int32_t *value)
{
    uint32_t index, degrees, minutes, seconds, arc_seconds;
    if (ef800_protocol_length(text) != 7u || value == 0) return PM_EINVAL;
    for (index = 0u; index < 7u; ++index)
        if (text[index] < '0' || text[index] > '9') return PM_EINVAL;
    degrees = (uint32_t)(text[0] - '0') * 100u +
              (uint32_t)(text[1] - '0') * 10u + (uint32_t)(text[2] - '0');
    minutes = (uint32_t)(text[3] - '0') * 10u + (uint32_t)(text[4] - '0');
    seconds = (uint32_t)(text[5] - '0') * 10u + (uint32_t)(text[6] - '0');
    if (minutes >= 60u || seconds >= 60u) return PM_EINVAL;
    arc_seconds = degrees * 3600u + minutes * 60u + seconds;
    /* gon * 10000 = arc-seconds * 250 / 81. */
    *value = (int32_t)divide_u32(arc_seconds * 250u, 81u);
    return PM_OK;
}

int32_t ef800_protocol_elta3_parse(char *line,
                                   struct ef800_measurement_result *result)
{
    char *tokens[5];
    uint32_t count = ef800_protocol_split(line, tokens, 5u);
    int32_t status;
    for (uint32_t index = 1u; index < count; ++index) {
        size_t length = ef800_protocol_length(tokens[index]);
        if (length != 0u && tokens[index][length - 1u] == ';') {
            tokens[index][length - 1u] = '\0';
        }
    }
    if (result == 0 || count == 0u || ef800_protocol_length(tokens[0]) < 3u ||
        tokens[0][1] < '0' || tokens[0][1] > '9' ||
        tokens[0][2] < '0' || tokens[0][2] > '9') return PM_EINVAL;
    result->control = (uint8_t)((tokens[0][1] - '0') * 10 + tokens[0][2] - '0');
    result->distance_mm = 0;
    if (result->control == 33u && count >= 3u) {
        status = ef800_protocol_parse_decimal(tokens[1], 10000u,
                                               &result->horizontal_gon_1e4);
        return status == PM_OK ? ef800_protocol_parse_decimal(
            tokens[2], 10000u, &result->vertical_gon_1e4) : status;
    }
    if (result->control != 48u || count < 5u) return PM_EINVAL;
    status = ef800_protocol_parse_decimal(tokens[2], 1000u, &result->distance_mm);
    if (status == PM_OK) status = ef800_protocol_parse_decimal(
        tokens[3], 10000u, &result->horizontal_gon_1e4);
    if (status == PM_OK) status = ef800_protocol_parse_decimal(
        tokens[4], 10000u, &result->vertical_gon_1e4);
    return status;
}

int32_t ef800_protocol_fts3xx_parse(char *line,
                                    struct ef800_measurement_result *result)
{
    char *tokens[3];
    int32_t status;
    if (result == 0 || ef800_protocol_split(line, tokens, 3u) < 3u) return PM_EINVAL;
    status = ef800_protocol_parse_decimal(tokens[0], 1u, &result->distance_mm);
    if (status == PM_OK) status = parse_fts_angle(tokens[1], &result->vertical_gon_1e4);
    if (status == PM_OK) status = parse_fts_angle(tokens[2], &result->horizontal_gon_1e4);
    if (status == PM_OK) result->control = 0u;
    return status;
}
