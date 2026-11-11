/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "ef800_measurement_plugin.h"
#include "ef800_plugin_ids.h"
#include "ef800_plugin_worker_sdk.h"
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

static struct ef800_plugin_worker_mailbox worker_mailbox;
static struct fake_port *active_port;
static uint32_t worker_sequence;
#define TEST_WORKER_GENERATION 7u
#define TEST_WORKER_CAPABILITY 0x7a51c0deu

int ef800_plugin_worker_syscall(
    struct ef800_plugin_worker_mailbox *mailbox)
{
    struct ef800_plugin_worker_message *message;

    assert(mailbox == &worker_mailbox);
    message = &mailbox->message;
    assert(ef800_plugin_worker_message_validate(message,
        EF800_PLUGIN_WORKER_MAILBOX_PAYLOAD_SIZE,
        TEST_WORKER_GENERATION, 100u) == 0);
    assert(message->kind == EF800_PLUGIN_WORKER_HOST_SERVICE_REQUEST);
    assert(message->capability == TEST_WORKER_CAPABILITY);
    if (message->sequence == 1u) worker_sequence = 0u;
    assert(message->sequence == ++worker_sequence);
    assert(active_port != NULL);

    switch (message->operation) {
    case EF800_PLUGIN_WORKER_PORT_WRITE:
        assert(active_port->output_size + message->payload_length <=
            sizeof(active_port->output));
        memcpy(active_port->output + active_port->output_size,
            mailbox->payload + message->payload_offset, message->payload_length);
        active_port->output_size += message->payload_length;
        message->status = 0;
        break;
    case EF800_PLUGIN_WORKER_PORT_READ:
        if (active_port->input_offset == active_port->input_size) {
            message->status = -ETIMEDOUT;
        } else {
            mailbox->payload[message->response_offset] =
                active_port->input[active_port->input_offset++];
            message->response_length = 1u;
            message->status = 0;
        }
        break;
    case EF800_PLUGIN_WORKER_LOG:
        assert(message->payload_length != 0u);
        assert(message->payload_length <= EF800_PLUGIN_WORKER_MAX_LOG_MESSAGE);
        message->status = 0;
        break;
    default:
        assert(false);
    }
    return 0;
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
    struct ef800_plugin_worker_binding binding = {
        .abi_version = EF800_PLUGIN_WORKER_BINDING_ABI_VERSION,
        .struct_size = sizeof(binding),
        .mailbox = &worker_mailbox,
        .generation = TEST_WORKER_GENERATION,
        .capability = TEST_WORKER_CAPABILITY,
        .deadline_ms = 10000u,
    };
    const struct pm_service service = {
        .service_id = {{EF800_PLUGIN_WORKER_SERVICE_UUID_BYTES}},
        .version = EF800_PLUGIN_WORKER_SERVICE_VERSION,
        .vtable = &binding,
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

    active_port = &fake;
    assert(pm_plugin_create(&table, &opaque) == PM_OK);
    instance = opaque;
    assert(instance != 0 && instance->api != 0);
    assert(pm_plugin_create(&table, &opaque) == PM_EBUSY);
    assert(pm_plugin_destroy((void *)((uintptr_t)instance + 1u)) == PM_EINVAL);
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
    fake.input_offset = 0u;
    assert(instance->api->handle_input(instance, EF800_MEASUREMENT_INPUT_ENTER) == PM_OK);
#else
    assert(instance->api->handle_input(instance, EF800_MEASUREMENT_INPUT_UP) == PM_ENOTSUP);
    assert(fake.output_size == sizeof(expected) - 1u);
    event.character = 'u';
    assert(instance->api->handle_key(instance, &event, &key_result) == PM_OK);
    assert(key_result == EF800_MEASUREMENT_KEY_UNSUPPORTED);
#endif
    {
        struct ef800_measurement_context context = {
            sizeof(context), 7, "CTX"
        };
        char record[128];
        uint32_t record_length = 0u;
        fake.input_offset = 0u;
        fake.output_size = 0u;
        assert(instance->api->set_context(instance, &context) == PM_OK);
        assert(instance->api->handle_input(instance, EF800_MEASUREMENT_INPUT_ENTER) == PM_OK);
        assert(instance->api->handle_input(instance, EF800_MEASUREMENT_INPUT_ENTER) == PM_OK);
        assert(instance->api->handle_input(instance, EF800_MEASUREMENT_INPUT_ENTER) == PM_OK);
        assert(instance->api->handle_input(instance, EF800_MEASUREMENT_INPUT_ENTER) == PM_OK);
        assert(instance->api->handle_input(instance, EF800_MEASUREMENT_INPUT_ENTER) == PM_OK);
        assert(instance->api->get_workspace_record(instance, record, sizeof(record),
            &record_length) == PM_OK);
        assert(record_length > 0u);
        assert(strstr(record, "CTX") != 0);
        assert(instance->api->workspace_record_result(instance, 0) == PM_OK);

        fake.input_offset = 0u;
        assert(instance->api->handle_input(instance, EF800_MEASUREMENT_INPUT_ENTER) == PM_OK);
        assert(instance->api->handle_input(instance, EF800_MEASUREMENT_INPUT_ENTER) == PM_OK);
        assert(instance->api->handle_input(instance, EF800_MEASUREMENT_INPUT_ENTER) == PM_OK);
        assert(instance->api->handle_input(instance, EF800_MEASUREMENT_INPUT_ENTER) == PM_OK);
        assert(instance->api->handle_input(instance, EF800_MEASUREMENT_INPUT_DOWN) == PM_OK);
        assert(instance->api->handle_input(instance, EF800_MEASUREMENT_INPUT_DOWN) == PM_OK);
        assert(instance->api->handle_input(instance, EF800_MEASUREMENT_INPUT_ENTER) == PM_OK);
        assert(instance->api->get_workspace_record(instance, record, sizeof(record),
            &record_length) == PM_OK);
        assert(strncmp(record, "8 ", 2u) == 0);
        fake.input_offset = 0u;
        assert(instance->api->workspace_record_result(instance, 0) == PM_OK);
        assert(instance->api->handle_input(instance, EF800_MEASUREMENT_INPUT_ENTER) == PM_OK);
        assert(instance->api->handle_input(instance, EF800_MEASUREMENT_INPUT_ENTER) == PM_OK);
        assert(instance->api->handle_input(instance, EF800_MEASUREMENT_INPUT_ENTER) == PM_OK);
        assert(instance->api->handle_input(instance, EF800_MEASUREMENT_INPUT_ENTER) == PM_OK);
        assert(instance->api->handle_input(instance, EF800_MEASUREMENT_INPUT_ENTER) == PM_OK);
        assert(instance->api->get_workspace_record(instance, record, sizeof(record),
            &record_length) == PM_OK);
        assert(strncmp(record, "9 ", 2u) == 0);
        assert(instance->api->workspace_record_result(instance, 0) == PM_OK);
    }
    assert(pm_plugin_destroy(instance) == PM_OK);
    assert(pm_plugin_create(&(struct pm_service_table){0u, 0}, &opaque) == PM_ENOENT);
    assert(pm_plugin_create(&table, &opaque) == PM_OK);
    assert(opaque != 0);
    assert(pm_plugin_destroy(opaque) == PM_OK);
    return 0;
}
