/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "ef800_measurement_plugin.h"
#include "ef800_plugin_ids.h"
#include "ef800_port_service.h"
#include "plugin_manager/plugin_abi.h"

int pm_plugin_create(const struct pm_service_table *services, void **instance);
int pm_plugin_destroy(void *instance);

struct fake_port {
    const uint8_t *input;
    uint32_t input_size;
    uint32_t input_offset;
    uint8_t output[64];
    uint32_t output_size;
};

static int32_t fake_write(void *context, const uint8_t *data, uint32_t length)
{
    struct fake_port *fake = context;
    assert(fake->output_size + length <= sizeof(fake->output));
    memcpy(fake->output + fake->output_size, data, length);
    fake->output_size += length;
    return (int32_t)length;
}

static int32_t fake_read(void *context, uint8_t *data, uint32_t capacity,
                         int32_t timeout_ms)
{
    struct fake_port *fake = context;
    (void)timeout_ms;
    if (capacity == 0u || fake->input_offset == fake->input_size) return 0;
    *data = fake->input[fake->input_offset++];
    return 1;
}

int main(void)
{
#if TEST_ELTA3
    static const uint8_t response[] = "A\r\n#48 0 1.234; 100.0000; 50.5000\r\n";
    static const uint8_t expected[] = "S14\r\nB\r\nQ\r\n";
#else
    static const uint8_t response[] = "1234 0452700 0900000\r\n";
    static const uint8_t expected[] = "\x11\r\n";
#endif
    struct fake_port fake = {response, sizeof(response) - 1u, 0u, {0u}, 0u};
    const struct ef800_port_api port = {
        EF800_PORT_API_VERSION, sizeof(struct ef800_port_api), &fake,
        fake_write, fake_read, {0u}
    };
    const struct pm_service service = {
        .service_id = {{EF800_PORT_SERVICE_UUID_BYTES}},
        .version = EF800_PORT_SERVICE_VERSION,
        .vtable = &port,
    };
    const struct pm_service_table table = {1u, &service};
    struct ef800_measurement_instance *instance;
    struct ef800_serial_defaults serial;
    struct ef800_measurement_result result;
    struct ef800_measurement_display display;
    struct ef800_measurement_key_event event = {
        sizeof(event), EF800_MEASUREMENT_KEY_CHARACTER, 0u, 'u', {0u}
    };
    uint32_t key_result;
    (void)key_result;
    void *opaque = 0;

    assert(pm_plugin_create(&table, &opaque) == PM_OK);
    instance = opaque;
    assert(instance != 0 && instance->api != 0);
    assert(instance->api->abi_version == EF800_MEASUREMENT_API_VERSION);
    assert(instance->api->get_serial_defaults(instance, &serial) == PM_OK);
    assert(serial.baudrate == TEST_BAUD && serial.data_bits == TEST_DATA_BITS);
    assert(serial.parity == TEST_PARITY && serial.stop_bits == TEST_STOP_BITS);
    assert(instance->api->measure(instance, &result) == PM_OK);
    assert(result.struct_size == sizeof(result));
    assert(result.distance_mm == 1234);
    assert(result.horizontal_gon_1e4 == 1000000);
    assert(result.vertical_gon_1e4 == 505000);
    assert(result.control == TEST_CONTROL);
    assert(instance->api->ui_abi_version == EF800_MEASUREMENT_UI_API_VERSION);
    assert(instance->api->render(instance, &display) == PM_OK);
    assert(ef800_measurement_display_validate(&display) == 0);
    display.row_count = EF800_MEASUREMENT_SCREEN_ROWS + 1u;
    assert(ef800_measurement_display_validate(&display) == PM_EINVAL);
    display.row_count = 1u;
    assert(fake.output_size == sizeof(expected) - 1u);
    assert(memcmp(fake.output, expected, sizeof(expected) - 1u) == 0);
#if TEST_HAS_INPUT
    assert(instance->api->handle_key(instance, &event, &key_result) == PM_OK);
    assert(key_result == EF800_MEASUREMENT_KEY_CONSUMED);
    assert(instance->api->handle_input(instance, EF800_MEASUREMENT_INPUT_UP) == PM_OK);
    assert(instance->api->handle_input(instance, EF800_MEASUREMENT_INPUT_DOWN) == PM_OK);
    assert(instance->api->handle_input(instance, EF800_MEASUREMENT_INPUT_ENTER) == PM_OK);
#else
    assert(instance->api->handle_input(instance, EF800_MEASUREMENT_INPUT_UP) == PM_ENOTSUP);
    event.character = 'u';
    assert(instance->api->handle_key(instance, &event, &key_result) == PM_OK);
    assert(key_result == EF800_MEASUREMENT_KEY_UNSUPPORTED);
#endif
    assert(pm_plugin_destroy(instance) == PM_OK);
    assert(pm_plugin_create(&(struct pm_service_table){0u, 0}, &opaque) == PM_ENOENT);
    return 0;
}
