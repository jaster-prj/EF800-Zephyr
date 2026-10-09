/* SPDX-License-Identifier: Apache-2.0 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

size_t strlen(const char *value)
{
    size_t length = 0u;
    if (value == 0) return 0u;
    while (value[length] != '\0') ++length;
    return length;
}

void *memset(void *destination, int value, size_t length)
{
    unsigned char *bytes = destination;
    while (length-- != 0u) *bytes++ = (unsigned char)value;
    return destination;
}

char *strcpy(char *destination, const char *source)
{
    char *result = destination;
    if (destination == 0 || source == 0) return destination;
    while ((*destination++ = *source++) != '\0') {}
    return result;
}

char *strncpy(char *destination, const char *source, size_t capacity)
{
    size_t index;
    if (destination == 0 || source == 0) return destination;
    for (index = 0u; index < capacity && source[index] != '\0'; ++index) {
        destination[index] = source[index];
    }
    while (index < capacity) destination[index++] = '\0';
    return destination;
}

long labs(long value)
{
    return value < 0L ? -value : value;
}

long strtol(const char *value, char **end, int base)
{
    long result = 0L;
    int sign = 1;
    const char *cursor = value;

    if (end != 0) *end = (char *)value;
    if (value == 0 || base != 10) return 0L;
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    if (*cursor == '-' || *cursor == '+') {
        if (*cursor++ == '-') sign = -1;
    }
    while (*cursor >= '0' && *cursor <= '9') {
        result = result * 10L + (long)(*cursor++ - '0');
        if (end != 0) *end = (char *)cursor;
    }
    return sign * result;
}

static void put_char(char *buffer, size_t capacity, size_t *written, char value)
{
    if (*written + 1u < capacity) buffer[*written] = value;
    ++*written;
}

static void put_number(char *buffer, size_t capacity, size_t *written,
    long value, unsigned int width)
{
    char digits[24];
    unsigned long magnitude = value < 0L ? (unsigned long)-value : (unsigned long)value;
    unsigned int count = 0u;

    do {
        digits[count++] = (char)('0' + magnitude % 10u);
        magnitude /= 10u;
    } while (magnitude != 0u && count < sizeof(digits));
    if (value < 0L) put_char(buffer, capacity, written, '-');
    while (count < width) {
        put_char(buffer, capacity, written, '0');
        --width;
    }
    while (count != 0u) put_char(buffer, capacity, written, digits[--count]);
}

int snprintf(char *buffer, size_t capacity, const char *format, ...)
{
    va_list args;
    size_t written = 0u;
    const char *cursor = format;

    if (buffer == 0 || capacity == 0u || format == 0) return -1;
    va_start(args, format);
    while (*cursor != '\0') {
        if (*cursor++ != '%') {
            put_char(buffer, capacity, &written, cursor[-1]);
            continue;
        }
        if (*cursor == '%') {
            put_char(buffer, capacity, &written, *cursor++);
            continue;
        }
        unsigned int width = 0u;
        if (*cursor == '0') ++cursor;
        while (*cursor >= '0' && *cursor <= '9') {
            width = width * 10u + (unsigned int)(*cursor++ - '0');
        }
        if (*cursor == 'l') ++cursor;
        if (*cursor == 'd') {
            put_number(buffer, capacity, &written, va_arg(args, long), width);
            ++cursor;
        } else if (*cursor == 's') {
            const char *value = va_arg(args, const char *);
            if (value != 0) while (*value != '\0') put_char(buffer, capacity, &written, *value++);
            ++cursor;
        }
    }
    va_end(args);
    buffer[written < capacity ? written : capacity - 1u] = '\0';
    return (int)written;
}
