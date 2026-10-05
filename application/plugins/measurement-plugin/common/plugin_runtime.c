/* SPDX-License-Identifier: Apache-2.0 */
#include "plugin_runtime.h"

#include "ef800_plugin_ids.h"

#include <errno.h>
#include <stddef.h>

struct ef800_plugin_runtime ef800_runtime;

static size_t text_length(const char *value, size_t capacity)
{
    size_t length = 0u;
    while (length < capacity && value[length] != '\0') ++length;
    return length;
}

static int text_copy(char *output, size_t capacity, const char *value)
{
    size_t length = text_length(value, capacity);
    if (length >= capacity) return -EOVERFLOW;
    for (size_t index = 0u; index <= length; ++index) output[index] = value[index];
    return 0;
}

void ef800_measurement_display_init(struct ef800_measurement_display *display,
    const char *title, uint32_t actions)
{
    if (display == 0) return;
    for (size_t index = 0u; index < sizeof(*display); ++index)
        ((uint8_t *)display)[index] = 0u;
    display->struct_size = sizeof(*display);
    display->actions = actions;
    (void)text_copy(display->title, sizeof(display->title), title);
    (void)text_copy(display->footer, sizeof(display->footer),
        "ENTER select  ESC back");
}

int ef800_measurement_display_number(char *output, uint32_t capacity,
    const char *label, int32_t value)
{
    char digits[12];
    uint32_t magnitude;
    uint32_t quotient;
    uint32_t remainder;
    size_t label_length = text_length(label, capacity);
    size_t digit_count = 0u;
    size_t position;

    if (output == 0 || capacity == 0u || label_length >= capacity) return -EINVAL;
    if (label_length + 2u >= capacity) return -EOVERFLOW;
    for (position = 0u; position < label_length; ++position) output[position] = label[position];
    position = label_length;
    output[position++] = ' ';
    magnitude = value < 0 ? (uint32_t)(0u - (uint32_t)value) : (uint32_t)value;
    do {
        quotient = 0u;
        remainder = 0u;
        for (int bit = 31; bit >= 0; --bit) {
            remainder = (remainder << 1) | ((magnitude >> (uint32_t)bit) & 1u);
            if (remainder >= 10u) {
                remainder -= 10u;
                quotient |= 1u << (uint32_t)bit;
            }
        }
        digits[digit_count++] = (char)('0' + remainder);
        magnitude = quotient;
    } while (magnitude != 0u && digit_count < sizeof(digits));
    if (value < 0) output[position++] = '-';
    while (digit_count != 0u) output[position++] = digits[--digit_count];
    if (position >= capacity) return -EOVERFLOW;
    output[position] = '\0';
    return 0;
}

int ef800_measurement_display_text(char *output, uint32_t capacity,
    const char *value)
{
    return output == 0 || value == 0 ? -EINVAL : text_copy(output, capacity, value);
}

int ef800_measurement_display_validate(
    const struct ef800_measurement_display *display)
{
    if (display == 0 || display->struct_size < sizeof(*display) ||
        display->row_count > EF800_MEASUREMENT_SCREEN_ROWS ||
        display->selected_action >= 32u ||
        text_length(display->title, sizeof(display->title)) >= sizeof(display->title) ||
        text_length(display->footer, sizeof(display->footer)) >= sizeof(display->footer)) {
        return -EINVAL;
    }
    for (uint32_t row = 0u; row < display->row_count; ++row) {
        if (text_length(display->rows[row], sizeof(display->rows[row])) >=
            sizeof(display->rows[row])) {
            return -EOVERFLOW;
        }
    }
    return 0;
}

static int uuid_equal(const pm_uuid_t *left, const pm_uuid_t *right)
{
    uint32_t index;
    for (index = 0u; index < PM_UUID_SIZE; ++index)
        if (left->bytes[index] != right->bytes[index]) return 0;
    return 1;
}

const struct ef800_port_api *ef800_find_port(const struct pm_service_table *services)
{
    size_t index;
    if (services == 0 || services->items == 0) return 0;
    for (index = 0u; index < services->count; ++index) {
        const struct pm_service *service = &services->items[index];
        if (uuid_equal(&service->service_id, &EF800_PORT_SERVICE_ID) &&
            service->version >= EF800_PORT_SERVICE_VERSION && service->vtable != 0) {
            const struct ef800_port_api *port = service->vtable;
            if (port->abi_version == EF800_PORT_API_VERSION &&
                port->struct_size >= sizeof(struct ef800_port_api) &&
                port->write != 0 && port->read != 0) return port;
        }
    }
    return 0;
}

int pm_plugin_create(const struct pm_service_table *services, void **instance)
{
    const struct ef800_port_api *port;
    if (instance == 0) return PM_EINVAL;
    port = ef800_find_port(services);
    if (port == 0) return PM_ENOENT;
    if (ef800_runtime.active != 0u) return PM_EBUSY;
    ef800_runtime.public_instance.api = &ef800_plugin_api;
    ef800_runtime.port = port;
    ef800_runtime.active = 1u;
    *instance = &ef800_runtime.public_instance;
    return PM_OK;
}

int pm_plugin_destroy(void *instance)
{
    if (instance != &ef800_runtime.public_instance || ef800_runtime.active == 0u)
        return PM_EINVAL;
    ef800_runtime.public_instance.api = 0;
    ef800_runtime.port = 0;
    ef800_runtime.active = 0u;
    return PM_OK;
}
