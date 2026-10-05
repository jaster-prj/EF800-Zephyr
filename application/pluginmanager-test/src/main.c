/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <ff.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys/printk.h>

#include <cmsis_core.h>
#include <stm32_ll_usart.h>
#include <soc.h>

#include "plugin_manager/plugin_abi.h"
#include "plugin_manager/plugin_descriptor.h"
#include "plugin_manager/plugin_manager.h"
#include "plugin_manager/plugin_manager_zephyr.h"
#include "plugin_manager/plugin_target_arm.h"
#include "ef800_measurement_plugin.h"
#include "ef800_plugin_ids.h"
#include "ef800_port_service.h"
#include "host_policy.h"
#include "calculation_plugin.h"
#include "calculation_service.h"

#define PLUGIN_DISK_NAME "PM"
#define PLUGIN_MOUNT_POINT "/PM:"
#define PLUGIN_DIRECTORY PLUGIN_MOUNT_POINT
#define ELTA3_FILE PLUGIN_MOUNT_POINT "/elta3.pmp"
#define FTS3XX_FILE PLUGIN_MOUNT_POINT "/fts3xx.pmp"
#define WORKSPACE_FILE PLUGIN_MOUNT_POINT "/measurements.dat"
#define TRANSFORM_FILE PLUGIN_MOUNT_POINT "/measurements-offset.dat"
#define SNAPSHOT_LIMIT 512u
#define TRANSFORM_TEMP_FILE PLUGIN_MOUNT_POINT "/measurements-offset.tmp"
#define PACKAGE_LIMIT (32u * 1024u)
#define IMAGE_TEXT_SIZE 0x2000u
#define IMAGE_RODATA_SIZE 0x2000u
#define IMAGE_DATA_SIZE 0x0800u
#define IMAGE_BSS_SIZE 0x0800u

extern const unsigned char elta3_package[];
extern const unsigned int elta3_package_size;
extern const unsigned char fts3xx_package[];
extern const unsigned int fts3xx_package_size;

/* The application overlay maps this service UART per target. */
static const struct device *const port_uart = DEVICE_DT_GET(DT_ALIAS(plugin_uart));
static FATFS plugin_fat;
static struct fs_mount_t plugin_mount = {
    .type = FS_FATFS,
    .mnt_point = PLUGIN_MOUNT_POINT,
    .fs_data = &plugin_fat,
#if defined(CONFIG_BOARD_QEMU_CORTEX_A9) || defined(CONFIG_BOARD_QEMU_CORTEX_M3) || \
    defined(CONFIG_BOARD_MPS2_AN385)
    /* The QEMU disk is disposable RAM; allow FatFS to create its volume. */
    .flags = 0,
#else
    .flags = FS_MOUNT_FLAG_NO_FORMAT,
#endif
};
static const struct pm_zephyr_filesystem plugin_filesystem = {
    .mount = &plugin_mount,
    /* Keep packages in the FAT root; FatFS short-name handling rejects the
     * former three-plus-character directory on the target volume. */
    .plugin_directory = "",
    .candidate_suffix = ".PMP",
    .max_entries = 4u,
    .max_name_length = 31u,
    .max_package_size = PACKAGE_LIMIT,
};

static uint8_t plugin_text[IMAGE_TEXT_SIZE] __aligned(4);
static uint8_t plugin_rodata[IMAGE_RODATA_SIZE] __aligned(4);
static uint8_t plugin_data[IMAGE_DATA_SIZE] __aligned(4);
static uint8_t plugin_bss[IMAGE_BSS_SIZE] __aligned(4);
static struct pm_plugin_descriptor descriptor;
static struct pm_manager *manager;
static struct ef800_measurement_instance *instance;
static struct ef800_serial_defaults serial_defaults;
static uint32_t selected_plugin_id;
static bool filesystem_mounted;
static bool filesystem_attached;
static bool storage_packages_validated;
static bool plugin_loaded;
static bool instance_created;
/* Diagnostic buffers are read-only snapshots of the pre-existing NOR volume. */
static struct ef800_calculation_instance *calculation;
static struct ef800_calculation_service calculation_service;
static uint8_t assigned_port[2] = {2u, 0u};
static char workspace_snapshot[SNAPSHOT_LIMIT + 1u];
static char transformed[SNAPSHOT_LIMIT + 1u];
static uint8_t port_rx_trace[128];
static size_t port_rx_trace_length;
static bool port_uses_seven_data_bits;
K_MUTEX_DEFINE(host_mutex);
K_MUTEX_DEFINE(manager_mutex);

static int manager_lock(void *context)
{
    return k_mutex_lock(context, K_FOREVER);
}

static void manager_unlock(void *context)
{
    (void)k_mutex_unlock(context);
}

static int32_t port_write(void *context, const uint8_t *data, uint32_t length)
{
    const struct device *uart = context;

    if (data == NULL && length != 0u) {
        return PM_EINVAL;
    }
    for (uint32_t index = 0u; index < length; ++index) {
        uart_poll_out(uart, data[index]);
    }
    return (int32_t)length;
}

static int32_t port_read(void *context, uint8_t *data, uint32_t capacity,
                         int32_t timeout_ms)
{
    const struct device *uart = context;
    int64_t deadline;
    uint32_t count = 0u;

    if (data == NULL && capacity != 0u) {
        return PM_EINVAL;
    }
    deadline = timeout_ms < 0 ? INT64_MAX : k_uptime_get() + timeout_ms;
    while (count < capacity) {
        unsigned char byte;

        if (uart_poll_in(uart, &byte) == 0) {
            if (port_uses_seven_data_bits) {
                byte &= 0x7fu;
            }
            data[count++] = byte;
            if (port_rx_trace_length < sizeof(port_rx_trace)) {
                port_rx_trace[port_rx_trace_length++] = byte;
            }
        } else if (count > 0u || k_uptime_get() >= deadline) {
            break;
        } else {
            k_sleep(K_MSEC(1));
        }
    }
    return (int32_t)count;
}

static const struct ef800_port_api port_api = {
    .abi_version = EF800_PORT_API_VERSION,
    .struct_size = sizeof(struct ef800_port_api),
    .context = (void *)port_uart,
    .write = port_write,
    .read = port_read,
};
static const struct pm_service services[] = {{
    .service_id = {{EF800_PORT_SERVICE_UUID_BYTES}},
    .version = EF800_PORT_SERVICE_VERSION,
    .vtable = &port_api,
}};
static const pm_uuid_t supported_services[] = {
    {{EF800_PORT_SERVICE_UUID_BYTES}},
};
static const struct pm_plugin_interface interfaces[] = {
    {
        .interface_id = {{EF800_MEASUREMENT_INTERFACE_UUID_BYTES}},
        .version = EF800_MEASUREMENT_INTERFACE_VERSION,
        .supported_service_ids = supported_services,
        .supported_service_count = ARRAY_SIZE(supported_services),
    },
    {
        .interface_id = {{EF800_CALCULATION_INTERFACE_UUID_BYTES}},
        .version = 1u,
    },
};
static const struct pm_application_contract contract = {
    .application_id = {{EF800_APPLICATION_UUID_BYTES}},
    .version = 1u,
    .interfaces = interfaces,
    .interface_count = ARRAY_SIZE(interfaces),
    .services = services,
    .service_count = ARRAY_SIZE(services),
};
static const struct pm_elf_profile elf_profile = {
    .max_sections = 32u,
    .max_image_size = IMAGE_TEXT_SIZE,
    .max_ram_size = IMAGE_DATA_SIZE,
    .text_address = 0u,
    .rodata_address = 0x2000u,
    .data_address = 0x20010000u,
};

static int allocate_plugin_image(void *context, size_t text_size,
                                 size_t rodata_size, size_t data_size,
                                 size_t bss_size, uint8_t **text,
                                 uint8_t **rodata, uint8_t **data,
                                 uint8_t **bss)
{
    ARG_UNUSED(context);
    if (text_size > sizeof(plugin_text) || rodata_size > sizeof(plugin_rodata) ||
        data_size > sizeof(plugin_data) || bss_size > sizeof(plugin_bss)) {
        return PM_EOVERFLOW;
    }
    *text = plugin_text;
    *rodata = plugin_rodata;
    *data = plugin_data;
    *bss = plugin_bss;
    return PM_OK;
}

static void free_plugin_image(void *context, uint8_t *text, uint8_t *rodata,
                              uint8_t *data, uint8_t *bss)
{
    ARG_UNUSED(context);
    ARG_UNUSED(text);
    ARG_UNUSED(rodata);
    ARG_UNUSED(data);
    ARG_UNUSED(bss);
}

static int convert_entry(void *context, uint32_t address,
                         enum pm_elf_entry_kind kind, void *function_out)
{
    const struct pm_elf_image *image = context;
    uintptr_t target = (uintptr_t)image->text +
                       ((address & ~1u) - image->text_address) +
                       (address & 1u);

    ARG_UNUSED(kind);
    *(void **)function_out = (void *)target;
    return PM_OK;
}

static int adapt_descriptor(const struct pm_wire_plugin_descriptor *wire,
                            const struct pm_elf_image *image,
                            const struct pm_package_manifest *manifest,
                            const struct pm_plugin_descriptor **result,
                            void *context)
{
    struct pm_arm_entry_adapter adapter = {
        .executable_base = image->text_address,
        .executable_size = image->text_size,
        .convert = convert_entry,
        .context = (void *)image,
    };
    struct pm_descriptor_bind_config bind = {
        .resolve = pm_arm_entry_resolve,
        .context = &adapter,
    };
    int status;

    ARG_UNUSED(manifest);
    ARG_UNUSED(context);
    status = pm_arm_entry_adapter_validate(&adapter, image);
    if (status == PM_OK) {
        status = pm_wire_descriptor_bind(wire, image, &bind, &descriptor);
    }
    if (status == PM_OK) {
        *result = &descriptor;
    }
    return status;
}

static const struct pm_manager_package_config package_config = {
    .elf_profile = &elf_profile,
    .adapt_wire_descriptor = adapt_descriptor,
    .alloc_image = allocate_plugin_image,
    .free_image = free_plugin_image,
};

static int write_package(const char *path, const uint8_t *bytes, size_t size,
                         bool overwrite)
{
    struct fs_dirent entry;
    struct fs_file_t file;
    ssize_t written;
    int status;

    status = fs_stat(path, &entry);
    if (status == 0 && !overwrite) {
        return 0;
    }
    if (status != 0 && status != -ENOENT) {
        return status;
    }
    fs_file_t_init(&file);
    status = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
    if (status != 0) {
        return status;
    }
    written = fs_write(&file, bytes, size);
    status = written == (ssize_t)size ? 0 :
             (written < 0 ? (int)written : -EIO);
    if (fs_close(&file) != 0 && status == 0) {
        status = -EIO;
    }
    return status;
}

static int install_examples(bool overwrite)
{
    int status;

    status = write_package(ELTA3_FILE, elta3_package, elta3_package_size,
                           overwrite);
    if (status == 0) {
        status = write_package(FTS3XX_FILE, fts3xx_package,
                               fts3xx_package_size, overwrite);
    }
    return status;
}

struct package_reader_context {
    const char *path;
    uint32_t package_size;
};

static int read_package_storage(void *context, uint32_t offset,
                                void *destination, size_t size)
{
    const struct package_reader_context *reader = context;
    struct fs_file_t file;
    ssize_t count;
    int status;

    if (reader == NULL || reader->path == NULL ||
        (destination == NULL && size != 0u) || offset > reader->package_size ||
        size > reader->package_size - offset) {
        return PM_EINVAL;
    }
    fs_file_t_init(&file);
    status = fs_open(&file, reader->path, FS_O_READ);
    if (status != 0) {
        return status == -ENOENT ? PM_ENOENT : PM_EIO;
    }
    status = fs_seek(&file, (off_t)offset, FS_SEEK_SET);
    if (status == 0 && size != 0u) {
        count = fs_read(&file, destination, size);
        status = count == (ssize_t)size ? 0 :
            (count < 0 ? (int)count : -EIO);
    }
    if (fs_close(&file) != 0 && status == 0) {
        status = -EIO;
    }
    return status == 0 ? PM_OK : PM_EIO;
}

static int validate_storage_package(const char *path)
{
    struct fs_dirent entry;
    struct package_reader_context reader;
    struct pm_package_manifest manifest;
    int status;

    status = fs_stat(path, &entry);
    if (status != 0) {
        return status == -ENOENT ? PM_ENOENT : PM_EIO;
    }
    if (entry.type != FS_DIR_ENTRY_FILE || entry.size < PM_PACKAGE_HEADER_SIZE ||
        (uint64_t)entry.size > UINT32_MAX) {
        return PM_EPROTO;
    }
    reader.path = path;
    reader.package_size = (uint32_t)entry.size;
    return pm_package_parse_reader(read_package_storage, &reader,
                                   reader.package_size, &manifest);
}

static int validate_storage_packages(void)
{
    int status = validate_storage_package(ELTA3_FILE);

    if (status == PM_OK) {
        status = validate_storage_package(FTS3XX_FILE);
    }
    storage_packages_validated = status == PM_OK;
    return status;
}

static int scan_plugins(void)
{
    int status = pm_manager_zephyr_scan(manager, &plugin_filesystem);

    return status;
}

static size_t plugin_slot(uint32_t id)
{
    return id == EF800_PLUGIN_ID_ELTA3 ? 0u : 1u;
}

static bool package_present(uint32_t id)
{
    struct pm_available_plugin available;

    for (size_t i = 0u; i < pm_manager_available_count(manager); ++i) {
        if (pm_manager_available_get(manager, i, &available) == PM_OK &&
            available.plugin_id == id &&
            pm_uuid_equal(&available.interface_id, &EF800_MEASUREMENT_INTERFACE_ID)) {
            return true;
        }
    }
    return false;
}

static enum host_menu_state measurement_state(uint32_t id)
{
    struct host_binding binding = {
        .plugin_id = id,
        .port_id = assigned_port[plugin_slot(id)],
        .route_ready = device_is_ready(port_uart),
        .package_available = package_present(id),
    };

    return host_measurement_state(&binding);
}

static bool selected_action_ready(void)
{
    struct host_binding binding;

    if (!instance_created) return false;
    binding.plugin_id = selected_plugin_id;
    binding.port_id = assigned_port[plugin_slot(selected_plugin_id)];
    binding.route_ready = device_is_ready(port_uart);
    binding.package_available = package_present(selected_plugin_id);
    return host_action_allowed(&binding, selected_plugin_id);
}

static int configure_port(const struct ef800_serial_defaults *defaults)
{
#if defined(CONFIG_BOARD_MPS2_AN385)
    ARG_UNUSED(defaults);
    return 0;
#else
    USART_TypeDef *usart = (USART_TypeDef *)DT_REG_ADDR(DT_ALIAS(plugin_uart));
    struct uart_config config = {
        .baudrate = defaults->baudrate,
        .parity = defaults->parity == EF800_SERIAL_PARITY_ODD ?
                      UART_CFG_PARITY_ODD :
                  defaults->parity == EF800_SERIAL_PARITY_EVEN ?
                      UART_CFG_PARITY_EVEN : UART_CFG_PARITY_NONE,
        .stop_bits = defaults->stop_bits == 2u ?
                         UART_CFG_STOP_BITS_2 : UART_CFG_STOP_BITS_1,
        .data_bits = defaults->data_bits == 7u ?
                         UART_CFG_DATA_BITS_7 : UART_CFG_DATA_BITS_8,
        /* Physical flow-control policy belongs to the application, not the
         * plugin ABI. The reference device route is always no-flow-control. */
        .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
    };

    /* Reset application-owned USART state on every plugin initialization so
     * a preceding 1200 baud plugin cannot leak DIV4 into a 9600 baud plugin. */
#if defined(USART_PRESC_PRESCALER) && defined(LL_USART_PRESCALER_DIV1)
    LL_USART_Disable(usart);
    LL_USART_SetPrescaler(usart, LL_USART_PRESCALER_DIV1);
#endif
    int status = uart_configure(port_uart, &config);
    port_uses_seven_data_bits = status == 0 && defaults->data_bits == 7u;

#if defined(USART_PRESC_PRESCALER) && defined(LL_USART_PRESCALER_DIV4)
    /* STM32G0 USART baud generation loses precision below 9600 baud at the
     * full APB clock. Slow the peripheral clock before recomputing BRR. */
    if (status == 0 && defaults->baudrate < 9600u) {
        /* The prescaler is part of the baud-rate calculation. Change it only
         * while USART is disabled; changing it after uart_configure() can
         * leave BRR and the active clock divider out of sync. */
        LL_USART_Disable(usart);
        LL_USART_SetPrescaler(usart, LL_USART_PRESCALER_DIV4);
        LL_USART_SetBaudRate(usart, 64000000u, LL_USART_PRESCALER_DIV4,
                             LL_USART_OVERSAMPLING_16, defaults->baudrate);
        LL_USART_Enable(usart);
    }
#endif
    return status;
#endif
}

static int unload_selected(void)
{
    int status;

    if (instance_created) {
        status = pm_manager_destroy_instance(manager, selected_plugin_id);
        if (status != PM_OK) {
            return status;
        }
        instance_created = false;
        instance = NULL;
    }
    if (plugin_loaded) {
        status = pm_manager_unload(manager, selected_plugin_id);
        if (status != PM_OK) {
            return status;
        }
        plugin_loaded = false;
    }
    selected_plugin_id = 0u;
    memset(&serial_defaults, 0, sizeof(serial_defaults));
    return PM_OK;
}

static int select_plugin(uint32_t wanted_id)
{
    struct pm_available_plugin available;
    void *opaque = NULL;
    uint32_t loaded_id = 0u;
    size_t count;
    int status;

    if (assigned_port[plugin_slot(wanted_id)] != 2u ||
        measurement_state(wanted_id) != HOST_MENU_READY) {
        return PM_ENOENT;
    }
    status = unload_selected();
    if (status != PM_OK) {
        return status;
    }
    count = pm_manager_available_count(manager);
    status = PM_ENOENT;
    for (size_t index = 0u; index < count; ++index) {
        if (pm_manager_available_get(manager, index, &available) == PM_OK &&
            available.plugin_id == wanted_id) {
            status = pm_manager_load_available(manager, index, &package_config,
                                               &loaded_id);
            break;
        }
    }
    if (status != PM_OK) {
        return status;
    }
    plugin_loaded = true;
    selected_plugin_id = loaded_id;
    __DSB();
    __ISB();
    status = pm_manager_create_instance(manager, loaded_id);
    if (status == PM_OK) {
        instance_created = true;
        status = pm_manager_get_instance(manager, loaded_id, &opaque);
    }
    if (status == PM_OK) {
        instance = opaque;
        if (instance == NULL || instance->api == NULL ||
            instance->api->abi_version != EF800_MEASUREMENT_API_VERSION ||
            instance->api->struct_size < sizeof(struct ef800_measurement_api) ||
            instance->api->get_serial_defaults == NULL ||
            instance->api->measure == NULL || instance->api->handle_input == NULL) {
            status = PM_EPROTO;
        }
    }
    if (status == PM_OK) {
        status = instance->api->get_serial_defaults(instance, &serial_defaults);
    }
    if (status == PM_OK) {
        status = configure_port(&serial_defaults);
    }
    if (status != PM_OK) {
        (void)unload_selected();
    }
    return status;
}

static int lock_host(const struct shell *shell)
{
    int status = k_mutex_lock(&host_mutex, K_FOREVER);

    if (status != 0) {
        shell_error(shell, "host lock failed: %d", status);
    }
    return status;
}

static int cmd_plugin_list(const struct shell *shell, size_t argc, char **argv)
{
    struct pm_available_plugin available;
    size_t count;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    if (lock_host(shell) != 0) {
        return -EIO;
    }
    count = pm_manager_available_count(manager);
    for (size_t index = 0u; index < count; ++index) {
        if (pm_manager_available_get(manager, index, &available) == PM_OK) {
            shell_print(shell, "%u: %s %s (%s, %u bytes)", available.plugin_id,
                        available.name, available.version,
                        available.storage_name, available.package_size);
        }
    }
    if (count == 0u) {
        shell_print(shell, "No plugins found");
    }
    for (size_t i = 0u; i < ARRAY_SIZE(assigned_port); ++i) {
        uint32_t id = i == 0u ? EF800_PLUGIN_ID_ELTA3 : EF800_PLUGIN_ID_FTS3XX;

        if (assigned_port[i] != 0u && !package_present(id)) {
            shell_warn(shell, "assignment plugin %u port %u: package missing",
                       id, assigned_port[i]);
        }
    }
    k_mutex_unlock(&host_mutex);
    return 0;
}

static int cmd_plugin_select(const struct shell *shell, size_t argc, char **argv)
{
    uint32_t plugin_id;
    int status;

    ARG_UNUSED(argc);
    plugin_id = argv[1][0] == '2' && argv[1][1] == '\0' ? EF800_PLUGIN_ID_ELTA3 :
                argv[1][0] == '3' && argv[1][1] == '\0' ? EF800_PLUGIN_ID_FTS3XX : 0u;
    if (plugin_id == 0u) {
        shell_error(shell, "expected plugin ID 2 or 3");
        return -EINVAL;
    }
    if (lock_host(shell) != 0) {
        return -EIO;
    }
    status = select_plugin(plugin_id);
    if (status == PM_OK) {
        shell_print(shell, "plugin %u selected: %s", plugin_id,
                    plugin_id == EF800_PLUGIN_ID_ELTA3 ? "ELTA3" : "FTS3XX");
    } else {
        shell_error(shell, "select failed: %d", status);
    }
    k_mutex_unlock(&host_mutex);
    return status == PM_OK ? 0 : -EIO;
}

static int cmd_plugin_assign(const struct shell *shell, size_t argc, char **argv)
{
    uint32_t id = argv[1][0] == '2' && argv[1][1] == '\0' ? EF800_PLUGIN_ID_ELTA3 :
                  argv[1][0] == '3' && argv[1][1] == '\0' ? EF800_PLUGIN_ID_FTS3XX : 0u;
    uint8_t port = argv[2][0] == '2' && argv[2][1] == '\0' ? 2u :
                   argv[2][0] == '0' && argv[2][1] == '\0' ? 0u : 255u;
    int status = PM_OK;

    ARG_UNUSED(argc);
    if (id == 0u || port == 255u) {
        return -EINVAL;
    }
    if (lock_host(shell) != 0) {
        return -EIO;
    }
    status = host_assignment_check(port, assigned_port[1u - plugin_slot(id)]);
    if (status != PM_OK) {
        shell_error(shell, "port %u already assigned", port);
    } else if (selected_plugin_id == id && plugin_loaded) {
        status = unload_selected();
    }
    if (status == PM_OK) {
        assigned_port[plugin_slot(id)] = port;
    }
    shell_print(shell, "plugin %u assigned port %u (status %d)", id, port, status);
    k_mutex_unlock(&host_mutex);
    return status == PM_OK ? 0 : -EIO;
}

static int cmd_plugin_menu(const struct shell *shell, size_t argc, char **argv)
{
    const uint32_t ids[] = {EF800_PLUGIN_ID_ELTA3, EF800_PLUGIN_ID_FTS3XX};

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    if (lock_host(shell) != 0) {
        return -EIO;
    }
    for (size_t i = 0u; i < ARRAY_SIZE(ids); ++i) {
        enum host_menu_state state = measurement_state(ids[i]);

        if (state != HOST_MENU_UNASSIGNED) {
            shell_print(shell, "%s (plugin %u, port %u): %s",
                        ids[i] == EF800_PLUGIN_ID_ELTA3 ? "ELTA3" : "FTS3XX",
                        ids[i], assigned_port[i],
                        state == HOST_MENU_READY ? "ready" :
                        state == HOST_MENU_MISSING ? "package missing" : "port unavailable");
        }
    }
    k_mutex_unlock(&host_mutex);
    return 0;
}

static int read_workspace_snapshot(void *context, char *destination,
                                   size_t capacity, size_t *length)
{
    struct fs_dirent entry;
    struct fs_file_t file;
    ssize_t read_count;
    int status;

    ARG_UNUSED(context);
    if (destination == NULL || length == NULL || capacity == 0u) return -EINVAL;
    if (!filesystem_mounted) return -ENODEV;
    status = fs_stat(WORKSPACE_FILE, &entry);

    if (status != 0) return status;
    if (entry.type != FS_DIR_ENTRY_FILE) return -EINVAL;
    if (entry.size > capacity) return -EFBIG;
    fs_file_t_init(&file);
    status = fs_open(&file, WORKSPACE_FILE, FS_O_READ);
    if (status != 0) return status;
    read_count = fs_read(&file, destination, capacity);
    status = read_count < 0 ? (int)read_count :
             read_count > (ssize_t)capacity ? -EFBIG :
             read_count == (ssize_t)entry.size ? 0 : -EIO;
    if (fs_close(&file) != 0 && status == 0) status = -EIO;
    if (status == 0) {
        *length = entry.size;
        if (*length < capacity) destination[*length] = '\0';
    }
    return status;
}

static int transform_calculation(void *context, const char *snapshot,
                                 size_t snapshot_length, int32_t offset,
                                 char *output, size_t capacity,
                                 size_t *output_length)
{
    ARG_UNUSED(context);
    if (calculation == NULL) return -ENODEV;
    return calculation->api->transform(calculation, snapshot, snapshot_length,
                                       offset, output, capacity, output_length);
}

static int run_calculation(int32_t offset, size_t *output_length)
{
    return ef800_calculation_service_run(&calculation_service, offset,
                                         workspace_snapshot,
                                         sizeof(workspace_snapshot),
                                         transformed, sizeof(transformed),
                                         output_length);
}

static int cmd_calc_preview(const struct shell *shell, size_t argc, char **argv)
{
    char *end;
    long offset;
    size_t output_length = 0u;
    int status;

    ARG_UNUSED(argc);
    offset = strtol(argv[1], &end, 10);
    if (*argv[1] == '\0' || *end != '\0' || offset < INT32_MIN || offset > INT32_MAX) {
        return -EINVAL;
    }
    if (lock_host(shell) != 0) return -EIO;
    status = run_calculation((int32_t)offset, &output_length);
    if (status == 0) {
        shell_print(shell, "Preview %u bytes from %s (no file written):\n%.*s",
                    (unsigned)output_length, WORKSPACE_FILE,
                    (int)output_length, transformed);
    } else {
        shell_error(shell, "calculation unavailable: %d", status);
    }
    k_mutex_unlock(&host_mutex);
    return status;
}

static int cmd_calc_status(const struct shell *shell, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    if (lock_host(shell) != 0) return -EIO;
    shell_print(shell, "Offset calculation: %s; input %s; output %s",
                calculation != NULL ? "ready" : "unavailable",
                WORKSPACE_FILE, TRANSFORM_FILE);
    k_mutex_unlock(&host_mutex);
    return 0;
}

static int cmd_calc_demo(const struct shell *shell, size_t argc, char **argv)
{
    static const char sample[] = "100\n-20\n";
    char *end;
    long offset = strtol(argv[1], &end, 10);
    size_t length = 0u;
    int status;

    ARG_UNUSED(argc);
    if (*argv[1] == '\0' || *end != '\0' || offset < INT32_MIN || offset > INT32_MAX) {
        return -EINVAL;
    }
    if (lock_host(shell) != 0) return -EIO;
    status = calculation == NULL ? -ENODEV :
        calculation->api->transform(calculation, sample, sizeof(sample) - 1u,
                                    (int32_t)offset, transformed,
                                    sizeof(transformed), &length);
    if (status == 0) {
        shell_print(shell, "Calculation demo (no storage):\n%.*s", (int)length, transformed);
    } else {
        shell_error(shell, "calculation demo failed: %d", status);
    }
    k_mutex_unlock(&host_mutex);
    return status;
}

static int cmd_calc_save(const struct shell *shell, size_t argc, char **argv)
{
    struct fs_file_t file;
    struct fs_dirent entry;
    char *end;
    long offset = strtol(argv[1], &end, 10);
    size_t output_length = 0u;
    ssize_t bytes;
    int status;

    ARG_UNUSED(argc);
    if (*argv[1] == '\0' || *end != '\0' || offset < INT32_MIN || offset > INT32_MAX ||
        strcmp(argv[2], "confirm") != 0) return -EINVAL;
    if (lock_host(shell) != 0) return -EIO;
    if (!filesystem_mounted) {
        k_mutex_unlock(&host_mutex);
        return -ENODEV;
    }
    status = fs_stat(TRANSFORM_FILE, &entry);
    if (status == 0) status = -EEXIST;
    if (status == -ENOENT) status = run_calculation((int32_t)offset, &output_length);
    if (status == 0) {
        status = fs_stat(TRANSFORM_TEMP_FILE, &entry);
        if (status == -ENOENT) status = 0;
        else if (status == 0) status = -EEXIST;
    }
    if (status == 0) {
        fs_file_t_init(&file);
        status = fs_open(&file, TRANSFORM_TEMP_FILE, FS_O_CREATE | FS_O_WRITE);
        if (status == 0) {
            bytes = fs_write(&file, transformed, output_length);
            status = bytes == (ssize_t)output_length ? 0 : bytes < 0 ? (int)bytes : -EIO;
            if (fs_close(&file) != 0 && status == 0) status = -EIO;
            if (status == 0) status = fs_rename(TRANSFORM_TEMP_FILE, TRANSFORM_FILE);
            if (status != 0) (void)fs_unlink(TRANSFORM_TEMP_FILE);
        }
    }
    shell_print(shell, "transformed file %s: %d", TRANSFORM_FILE, status);
    k_mutex_unlock(&host_mutex);
    return status;
}

static int cmd_plugin_status(const struct shell *shell, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    if (lock_host(shell) != 0) {
        return -EIO;
    }
    shell_print(shell, "mounted=%s attached=%s storage_validated=%s loaded=%s instance=%s selected=%u",
                filesystem_mounted ? "yes" : "no",
                filesystem_attached ? "yes" : "no",
                storage_packages_validated ? "yes" : "no",
                plugin_loaded ? "yes" : "no",
                instance_created ? "yes" : "no", selected_plugin_id);
    k_mutex_unlock(&host_mutex);
    return 0;
}

static int cmd_plugin_storage_check(const struct shell *shell, size_t argc,
                                    char **argv)
{
    int status;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    if (lock_host(shell) != 0) {
        return -EIO;
    }
    if (!filesystem_mounted || !filesystem_attached) {
        storage_packages_validated = false;
        shell_error(shell, "storage unavailable");
        k_mutex_unlock(&host_mutex);
        return -ENODEV;
    }
    status = validate_storage_packages();
    shell_print(shell, status == PM_OK ?
                "storage packages valid: reader CRC checks passed" :
                "storage package validation failed: %d", status);
    k_mutex_unlock(&host_mutex);
    return status == PM_OK ? 0 : -EIO;
}

static int cmd_plugin_unload(const struct shell *shell, size_t argc, char **argv)
{
    int status;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    if (lock_host(shell) != 0) {
        return -EIO;
    }
    status = unload_selected();
    shell_print(shell, status == PM_OK ? "plugin unloaded" : "unload failed: %d",
                status);
    k_mutex_unlock(&host_mutex);
    return status == PM_OK ? 0 : -EIO;
}

static int cmd_plugin_rescan(const struct shell *shell, size_t argc, char **argv)
{
    int status;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    if (lock_host(shell) != 0) {
        return -EIO;
    }
    status = unload_selected();
    if (status == PM_OK) {
        status = scan_plugins();
    }
    shell_print(shell, status == PM_OK ? "plugin scan complete" : "scan failed: %d",
                status);
    k_mutex_unlock(&host_mutex);
    return status == PM_OK ? 0 : -EIO;
}

static int cmd_plugin_reinstall(const struct shell *shell, size_t argc, char **argv)
{
    int status;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    if (lock_host(shell) != 0) {
        return -EIO;
    }
    status = unload_selected();
    if (status == PM_OK) {
        status = install_examples(true);
    }
    if (status == 0) {
        status = scan_plugins();
    }
    shell_print(shell, status == PM_OK ? "examples reinstalled" :
                "reinstall failed: %d", status);
    k_mutex_unlock(&host_mutex);
    return status == PM_OK ? 0 : -EIO;
}

static int cmd_port_status(const struct shell *shell, size_t argc, char **argv)
{
    struct uart_config config;
    int status;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    if (lock_host(shell) != 0) {
        return -EIO;
    }
    status = uart_config_get(port_uart, &config);
    if (status == 0) {
        shell_print(shell, "device UART ready=%s baud=%u data=%u parity=%u stop=%u flow=%u",
                    device_is_ready(port_uart) ? "yes" : "no", config.baudrate,
                    config.data_bits, config.parity, config.stop_bits,
                    config.flow_ctrl);
    } else {
        shell_error(shell, "port status failed: %d", status);
    }
    k_mutex_unlock(&host_mutex);
    return status;
}

static int cmd_port_probe(const struct shell *shell, size_t argc, char **argv)
{
    static const uint8_t request[] = {'S', '1', '4', '\r'};
    uint8_t response[8];
    uint32_t count = 0u;
    int64_t deadline;
    int status;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    if (lock_host(shell) != 0) return -EIO;
    status = configure_port(&(struct ef800_serial_defaults){
        .struct_size = sizeof(struct ef800_serial_defaults),
        .baudrate = 1200u,
        .data_bits = 7u,
        .parity = EF800_SERIAL_PARITY_ODD,
        .stop_bits = 2u,
        .flow_control = EF800_SERIAL_FLOW_NONE,
    });
    if (status == 0) {
        status = port_write((void *)port_uart, request, sizeof(request));
    }
    deadline = k_uptime_get() + 2000;
    while (status == 0 && count < ARRAY_SIZE(response) &&
           k_uptime_get() < deadline) {
        unsigned char byte;

        if (uart_poll_in(port_uart, &byte) == 0) {
            response[count++] = byte;
        } else {
            k_sleep(K_MSEC(1));
        }
    }
    if (status == 0) {
        shell_fprintf(shell, SHELL_NORMAL, "device probe rx %u bytes:", count);
        for (uint32_t index = 0u; index < count; ++index) {
            shell_fprintf(shell, SHELL_NORMAL, " %02x", response[index]);
        }
        shell_print(shell, "");
        status = count == 0u ? -ETIMEDOUT : 0;
    }
    k_mutex_unlock(&host_mutex);
    return status;
}

static int cmd_port_probe_fennel(const struct shell *shell, size_t argc,
                                 char **argv)
{
    static const uint8_t request[] = {0x11u, '\r', '\n'};
    uint8_t response[64];
    uint32_t count = 0u;
    int64_t deadline;
    struct ef800_serial_defaults defaults = {
        .struct_size = sizeof(defaults),
        .baudrate = 9600u,
        .data_bits = 8u,
        .parity = EF800_SERIAL_PARITY_NONE,
        .stop_bits = 1u,
        .flow_control = EF800_SERIAL_FLOW_NONE,
    };
    int status;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    if (lock_host(shell) != 0) return -EIO;
    status = configure_port(&defaults);
    if (status == 0) status = port_write((void *)port_uart, request, sizeof(request));
    deadline = k_uptime_get() + 2000;
    while (status == 0 && count < ARRAY_SIZE(response) &&
           k_uptime_get() < deadline) {
        unsigned char byte;

        if (uart_poll_in(port_uart, &byte) == 0) response[count++] = byte;
        else k_sleep(K_MSEC(1));
    }
    shell_fprintf(shell, SHELL_NORMAL, "fennel probe rx %u bytes:", count);
    for (uint32_t index = 0u; index < count; ++index) {
        shell_fprintf(shell, SHELL_NORMAL, " %02x", response[index]);
    }
    shell_print(shell, "");
    k_mutex_unlock(&host_mutex);
    return status == 0 && count > 0u ? 0 : -ETIMEDOUT;
}

static int invoke_input(const struct shell *shell, uint32_t input,
                        const char *name)
{
    int status;

    if (lock_host(shell) != 0) {
        return -EIO;
    }
    shell_print(shell, "KB: %s", name);
    if (!selected_action_ready()) {
        status = PM_ENOENT;
    } else if (input == 0u) {
        status = PM_OK; /* Cancel is host-only; SDK v1 has no cancel event. */
    } else {
        status = instance->api->handle_input(instance, input);
    }
    if (status == PM_ENOTSUP) {
        shell_warn(shell, "selected plugin does not support %s", name);
    } else if (status != PM_OK) {
        shell_error(shell, "KB input failed: %d", status);
    }
    k_mutex_unlock(&host_mutex);
    return status == PM_OK || status == PM_ENOTSUP ? 0 : -EIO;
}

static int cmd_kb_measure(const struct shell *shell, size_t argc, char **argv)
{
    struct ef800_measurement_result result;
    int status;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    if (lock_host(shell) != 0) {
        return -EIO;
    }
    port_rx_trace_length = 0u;
    shell_print(shell, "KB: Measure");
    status = selected_action_ready() ? instance->api->measure(instance, &result) : PM_ENOENT;
    if (status == PM_OK) {
        shell_print(shell, "distance_mm=%d horizontal_gon_1e4=%d "
                    "vertical_gon_1e4=%d control=%u flags=0x%x",
                    result.distance_mm, result.horizontal_gon_1e4,
                    result.vertical_gon_1e4, result.control, result.flags);
    } else {
        shell_error(shell, "measure failed: %d", status);
        shell_fprintf(shell, SHELL_NORMAL, "device rx %u bytes:",
                      (unsigned)port_rx_trace_length);
        for (size_t index = 0u; index < port_rx_trace_length; ++index) {
            shell_fprintf(shell, SHELL_NORMAL, " %02x", port_rx_trace[index]);
        }
        shell_print(shell, "");
    }
    k_mutex_unlock(&host_mutex);
    return status == PM_OK ? 0 : -EIO;
}

#define KB_INPUT_COMMAND(name, value, display_name)                             \
    static int cmd_kb_##name(const struct shell *shell, size_t argc, char **argv) \
    {                                                                            \
        ARG_UNUSED(argc);                                                         \
        ARG_UNUSED(argv);                                                         \
        return invoke_input(shell, value, display_name);                          \
    }

KB_INPUT_COMMAND(up, EF800_MEASUREMENT_INPUT_UP, "Up")
KB_INPUT_COMMAND(down, EF800_MEASUREMENT_INPUT_DOWN, "Down")
KB_INPUT_COMMAND(enter, EF800_MEASUREMENT_INPUT_ENTER, "Enter")
KB_INPUT_COMMAND(cancel, 0u, "Cancel")

SHELL_STATIC_SUBCMD_SET_CREATE(plugin_commands,
    SHELL_CMD(list, NULL, "List discovered packages", cmd_plugin_list),
    SHELL_CMD(menu, NULL, "Show assigned measurement actions and availability", cmd_plugin_menu),
    SHELL_CMD_ARG(assign, NULL, "Assign <2|3> to port <2|0>; 0 disables", cmd_plugin_assign, 3, 0),
    SHELL_CMD_ARG(select, NULL, "Select and instantiate <2|3>",
                  cmd_plugin_select, 2, 0),
    SHELL_CMD(status, NULL, "Show host lifecycle state", cmd_plugin_status),
    SHELL_CMD(storage-check, NULL, "Validate packages from mounted storage",
              cmd_plugin_storage_check),
    SHELL_CMD(unload, NULL, "Destroy and unload selected plugin",
              cmd_plugin_unload),
    SHELL_CMD(rescan, NULL, "Unload and rescan FAT packages", cmd_plugin_rescan),
    SHELL_CMD(reinstall-examples, NULL, "Replace embedded example packages",
              cmd_plugin_reinstall),
    SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(plugin, &plugin_commands, "PluginManager commands", NULL);

SHELL_STATIC_SUBCMD_SET_CREATE(calc_commands,
    SHELL_CMD(status, NULL, "Show calculation action", cmd_calc_status),
    SHELL_CMD_ARG(demo, NULL, "Run static calculation without storage", cmd_calc_demo, 2, 0),
    SHELL_CMD_ARG(preview, NULL, "Preview millimetre offset on measurements.dat", cmd_calc_preview, 2, 0),
    SHELL_CMD_ARG(save, NULL, "Save offset <mm> confirm to new transformed file", cmd_calc_save, 3, 0),
    SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(calc, &calc_commands, "Workspace calculation prototype", NULL);

SHELL_STATIC_SUBCMD_SET_CREATE(port_commands,
    SHELL_CMD(status, NULL, "Show USART1 configuration", cmd_port_status),
    SHELL_CMD(probe, NULL, "Send raw ELTA3 request and show response bytes",
              cmd_port_probe),
    SHELL_CMD(probe-fennel, NULL, "Send raw Fennel request and show response bytes",
              cmd_port_probe_fennel),
    SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(port, &port_commands, "Port service commands", NULL);

SHELL_STATIC_SUBCMD_SET_CREATE(kb_commands,
    SHELL_CMD(measure, NULL, "Run a measurement", cmd_kb_measure),
    SHELL_CMD(up, NULL, "Send Up", cmd_kb_up),
    SHELL_CMD(down, NULL, "Send Down", cmd_kb_down),
    SHELL_CMD(enter, NULL, "Send Enter", cmd_kb_enter),
    SHELL_CMD(cancel, NULL, "Host-only cancel", cmd_kb_cancel),
    SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(kb, &kb_commands, "Keyboard-style plugin commands", NULL);

static int initialize_host(void)
{
    const struct pm_manager_config manager_config = {
        .contract = &contract,
        .max_plugins = 2u,
        .lock = manager_lock,
        .unlock = manager_unlock,
        .lock_context = &manager_mutex,
    };
    int status;

    if (!device_is_ready(port_uart)) {
        return -ENODEV;
    }
    manager = pm_manager_create(&manager_config);
    if (manager == NULL) {
        return -ENOMEM;
    }
    status = pm_manager_register_static(manager, &ef800_offset_calculation_descriptor);
    if (status != PM_OK) return status;
    status = pm_manager_create_instance(manager, EF800_CALCULATION_PLUGIN_ID);
    if (status != PM_OK) return status;
    {
        void *opaque = NULL;

        status = pm_manager_get_instance(manager, EF800_CALCULATION_PLUGIN_ID, &opaque);
        if (status != PM_OK || opaque == NULL) return -EIO;
        calculation = opaque;
        if (calculation->api == NULL || calculation->api->abi_version != 1u ||
            calculation->api->struct_size < sizeof(struct ef800_calculation_api) ||
            calculation->api->transform == NULL) return -EPROTO;
        calculation_service = (struct ef800_calculation_service){
            .snapshot = read_workspace_snapshot,
            .transform = transform_calculation,
            .context = NULL,
        };
    }
    status = disk_access_init(PLUGIN_DISK_NAME);
    if (status != 0) {
        printk("PM disk unavailable: %d; storage actions disabled\n", status);
        return 0;
    }
#if !defined(CONFIG_BOARD_QEMU_CORTEX_A9) && !defined(CONFIG_BOARD_QEMU_CORTEX_M3) && \
    !defined(CONFIG_BOARD_MPS2_AN385)
    plugin_mount.flags |= FS_MOUNT_FLAG_NO_FORMAT;
#endif
    status = fs_mount(&plugin_mount);
    if (status != 0) {
        printk("PM remount failed: %d; storage actions disabled\n", status);
        return 0;
    }
    filesystem_mounted = true;
    status = install_examples(false);
    if (status != 0) return status;
    status = validate_storage_packages();
    if (status != PM_OK) {
        printk("PM package reader validation failed: %d; storage actions disabled\n",
               status);
        return 0;
    }
    status = pm_manager_zephyr_attach_filesystem(manager, &plugin_filesystem);
    if (status != PM_OK) {
        return status;
    }
    filesystem_attached = true;
    status = scan_plugins();
    return status;
}

static void shutdown_host(void)
{
    (void)unload_selected();
    if (calculation != NULL) {
        (void)pm_manager_destroy_instance(manager, EF800_CALCULATION_PLUGIN_ID);
        calculation = NULL;
    }
    if (manager != NULL) {
        /* Static registration blocks detach in v1; destruction releases all
         * manager-owned storage references before the FAT unmount. */
        pm_manager_destroy(manager);
        manager = NULL;
    }
    filesystem_attached = false;
    if (filesystem_mounted) {
        (void)fs_unmount(&plugin_mount);
        filesystem_mounted = false;
    }
}

int main(void)
{
    int status;

    printk("PluginManager NUCLEO host starting\n");
    status = initialize_host();
    if (status != 0) {
        printk("PluginManager host initialization failed: %d\n", status);
        shutdown_host();
        return status;
    }
    printk("PluginManager host ready; use 'plugin list'\n");
    return 0;
}
