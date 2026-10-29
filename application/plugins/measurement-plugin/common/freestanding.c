/* SPDX-License-Identifier: Apache-2.0 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__arm__) || defined(__thumb__)
static uint32_t unsigned_divide(uint32_t dividend, uint32_t divisor,
    uint32_t *remainder)
{
    uint32_t quotient = 0u;
    uint32_t bit;
    uint32_t quotient_bit = 1u << 31;
    uint64_t partial = 0u;

    if (divisor == 0u) {
        if (remainder != 0) *remainder = 0u;
        return 0u;
    }
    for (bit = 32u; bit != 0u; --bit) {
        partial = (partial << 1) | ((dividend >> (bit - 1u)) & 1u);
        if (partial >= divisor) {
            partial -= divisor;
            quotient |= quotient_bit;
        }
        quotient_bit >>= 1;
    }
    if (remainder != 0) *remainder = (uint32_t)partial;
    return quotient;
}

uint32_t __aeabi_uidiv(uint32_t dividend, uint32_t divisor)
{
    return unsigned_divide(dividend, divisor, 0);
}

uint32_t __aeabi_uidivmod(uint32_t dividend, uint32_t divisor)
{
    uint32_t remainder;
    uint32_t quotient = unsigned_divide(dividend, divisor, &remainder);

    __asm volatile("mov r1, %0" : : "r" (remainder));
    return quotient;
}

int32_t __aeabi_idiv(int32_t dividend, int32_t divisor)
{
    unsigned int negative = ((dividend < 0) != (divisor < 0));
    uint32_t magnitude_dividend = dividend < 0 ? (uint32_t)-dividend : (uint32_t)dividend;
    uint32_t magnitude_divisor = divisor < 0 ? (uint32_t)-divisor : (uint32_t)divisor;
    int32_t result = (int32_t)unsigned_divide(magnitude_dividend,
        magnitude_divisor, 0);

    return negative ? -result : result;
}

int32_t __aeabi_idivmod(int32_t dividend, int32_t divisor)
{
    uint32_t remainder;
    unsigned int negative = ((dividend < 0) != (divisor < 0));
    uint32_t magnitude_dividend = dividend < 0 ? (uint32_t)-dividend : (uint32_t)dividend;
    uint32_t magnitude_divisor = divisor < 0 ? (uint32_t)-divisor : (uint32_t)divisor;
    int32_t quotient = (int32_t)unsigned_divide(magnitude_dividend,
        magnitude_divisor, &remainder);
    int32_t signed_remainder = (int32_t)remainder;

    __asm volatile("mov r1, %0" : : "r" (negative ? -signed_remainder : signed_remainder));
    return negative ? -quotient : quotient;
}

int32_t __aeabi_idiv0(int32_t value)
{
    (void)value;
    return 0;
}
#endif

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
