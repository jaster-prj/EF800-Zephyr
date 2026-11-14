/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Implement common measurement-plugin lifecycle and UI callbacks. */
#include "plugin_runtime.h"

#include "ef800_plugin_ids.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

struct ef800_plugin_runtime ef800_runtime;

int32_t ef800_plugin_port_write(ef800_port_binding *port,
	const uint8_t *data, uint32_t length)
{
	if (port == 0) return PM_EINVAL;
#if EF800_PLUGIN_EXECUTION_MODEL == EF800_PLUGIN_EXECUTION_LEGACY_DIRECT
	if (port->api == 0 || port->api->write == 0) return PM_EINVAL;
	return port->api->write(port->api->context, data, length);
#else
	return ef800_plugin_worker_port_write(port, data, length);
#endif
}

int32_t ef800_plugin_port_read(ef800_port_binding *port, uint8_t *data,
	uint32_t capacity, uint32_t *read_count, int32_t timeout_ms)
{
	if (port == 0 || read_count == 0) return PM_EINVAL;
#if EF800_PLUGIN_EXECUTION_MODEL == EF800_PLUGIN_EXECUTION_LEGACY_DIRECT
	if (port->api == 0 || port->api->read == 0) return PM_EINVAL;
	int32_t count = port->api->read(port->api->context, data, capacity,
		timeout_ms);
	if (count < 0) return count;
	*read_count = (uint32_t)count;
	return PM_OK;
#else
	return ef800_plugin_worker_port_read(port, data, capacity, read_count,
		timeout_ms);
#endif
}

int32_t ef800_plugin_runtime_log(const char *message, uint32_t length)
{
#if EF800_PLUGIN_EXECUTION_MODEL == EF800_PLUGIN_EXECUTION_LEGACY_DIRECT
	(void)message;
	(void)length;
	return PM_OK;
#else
	return ef800_plugin_worker_log(&ef800_runtime.binding, message, length);
#endif
}

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
    memset(display, 0, sizeof(*display));
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
        uint32_t quotient = 0u;
        uint32_t remainder = 0u;
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

int pm_plugin_create(const struct pm_service_table *services, void **instance)
{
#if EF800_PLUGIN_EXECUTION_MODEL == EF800_PLUGIN_EXECUTION_USER_WORKER
    struct ef800_plugin_worker_binding binding;
#else
	static const pm_uuid_t port_service_id = {
		{EF800_PLUGIN_PORT_SERVICE_UUID_BYTES},
	};
	const struct ef800_port_api *api;
#endif
#if EF800_PLUGIN_EXECUTION_MODEL == EF800_PLUGIN_EXECUTION_USER_WORKER
	int status;
#endif

    if (instance == 0) return PM_EINVAL;
    if (services == 0) return PM_EINVAL;
    if (services->count == 0u) return PM_ENOENT;
    if (services->items == 0) return PM_EPROTO;
#if EF800_PLUGIN_EXECUTION_MODEL == EF800_PLUGIN_EXECUTION_USER_WORKER
	status = ef800_plugin_worker_binding_from_services(services, &binding);
    if (status == -ENOENT) return PM_ENOENT;
    if (status != 0)
        return PM_EPROTO;
#else
	uint32_t service_id_matches = 1u;
	if (services->count != 1u || services->items == 0 ||
		services->items[0].vtable == 0) {
		return PM_ENOENT;
	}
	for (size_t index = 0u; index < sizeof(port_service_id); ++index) {
		if (((const uint8_t *)&services->items[0].service_id)[index] !=
			((const uint8_t *)&port_service_id)[index]) {
			service_id_matches = 0u;
			break;
		}
	}
	if (service_id_matches == 0u) return PM_ENOENT;
	api = services->items[0].vtable;
	if (api->abi_version != EF800_PORT_API_VERSION ||
		api->struct_size < sizeof(*api) || api->write == 0 || api->read == 0) {
		return PM_EPROTO;
	}
#endif
    if (ef800_runtime.active != 0u) return PM_EBUSY;
    ef800_runtime.public_instance.api = &ef800_plugin_api;
#if EF800_PLUGIN_EXECUTION_MODEL == EF800_PLUGIN_EXECUTION_USER_WORKER
	ef800_runtime.binding = binding;
#else
	ef800_runtime.binding.api = api;
#endif
    ef800_runtime.active = 1u;
    *instance = &ef800_runtime.public_instance;
    return PM_OK;
}

/* cppcheck-suppress constParameterPointer -- plugin manager ABI requires void * */
int pm_plugin_destroy(void *instance)
{
    if (instance != &ef800_runtime.public_instance || ef800_runtime.active == 0u)
        return PM_EINVAL;
    ef800_runtime.public_instance.api = 0;
    memset(&ef800_runtime.binding, 0, sizeof(ef800_runtime.binding));
    ef800_runtime.active = 0u;
    return PM_OK;
}
