/* SPDX-License-Identifier: Apache-2.0 */

#include "peripheral_tests.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>
#include <zephyr/usb/class/usbd_msc.h>
#include <zephyr/usb/usbd.h>
#include <ff.h>

#include <stm32l4xx_hal.h>
#include <stm32l4xx_hal_uart.h>

#include "ef800_t6963.h"
#include "flash.h"
#include "keyboard_protocol.h"
#include "test_result.h"
#include "ef800_composition.h"

#define LCD_NODE DT_PATH(zephyr_user)
#define TEST_UART_TIMEOUT_MS 1000U
#define KEYBOARD_RESET_NODE DT_PATH(zephyr_user)
#define STORAGE_DISK_NAME "DATA"
#define STORAGE_MOUNT_POINT "/DATA:"
#define CONFIGURATION_DISK_NAME "EF800"
#define CONFIGURATION_MOUNT_POINT "/EF800:"
#define CONFIGURATION_PROFILE_DIRECTORY CONFIGURATION_MOUNT_POINT "/.configuration"
#define CONFIGURATION_PROFILE_A CONFIGURATION_PROFILE_DIRECTORY "/target_profile_a"
#define CONFIGURATION_PROFILE_B CONFIGURATION_PROFILE_DIRECTORY "/target_profile_b"
#define STORAGE_DIR STORAGE_MOUNT_POINT "/storage_test"
#define STORAGE_FAT_FILE STORAGE_DIR "/created_by_fat.txt"
#define STORAGE_DUMMY_FILE STORAGE_DIR "/dummy.txt"
#define STORAGE_MSC_FILE STORAGE_DIR "/created_by_msc.txt"
#define STORAGE_FAT_CONTENT "created by internal FAT\r\n"
#define STORAGE_DUMMY_CONTENT "EF800 storage test dummy\r\n"
#define STORAGE_MSC_CONTENT "created by USB MSC\r\n"
#define QSPI_FS_SECTOR_SIZE 512U
#define CDC_TEST_DURATION_MS 60000U

#define STORAGE_USB_UDC_NODE DT_NODELABEL(usbotg_fs)
#define STORAGE_FLASH_NODE DT_NODELABEL(qspi_nor)
BUILD_ASSERT(DT_NODE_HAS_STATUS(STORAGE_USB_UDC_NODE, okay),
	"hwdebug storage test requires USB device controller");

USBD_DEVICE_DEFINE(hwdebug_usbd, DEVICE_DT_GET(STORAGE_USB_UDC_NODE),
	CONFIG_EF800_USB_VID, CONFIG_EF800_USB_PID);
USBD_DESC_LANG_DEFINE(hwdebug_usb_lang);
USBD_DESC_MANUFACTURER_DEFINE(hwdebug_usb_manufacturer, "EF800");
USBD_DESC_PRODUCT_DEFINE(hwdebug_usb_product, "EF800 Storage Test");
USBD_DESC_CONFIG_DEFINE(hwdebug_usb_config_desc, "Storage MSC");
USBD_CONFIGURATION_DEFINE(hwdebug_usb_config, 0, 125,
	&hwdebug_usb_config_desc);
USBD_DEFINE_MSC_LUN(hwdebug_qspi_lun, STORAGE_DISK_NAME, "EF800",
	"Storage Test", "1.00");

static FATFS storage_fat_fs;
static FATFS configuration_fat_fs;
static struct ef800_composition_profile qspi_format_profile;
static uint8_t qspi_format_record[EF800_COMPOSITION_RECORD_MAX];
static uint8_t qspi_format_sector[QSPI_FS_SECTOR_SIZE];
static uint8_t qspi_format_raw_sector[QSPI_FS_SECTOR_SIZE];
static struct fs_mount_t storage_mount = {
	.type = FS_FATFS,
	.mnt_point = STORAGE_MOUNT_POINT,
	.fs_data = &storage_fat_fs,
	.storage_dev = (void *)PARTITION_ID(qspi_storage_partition),
	.flags = FS_MOUNT_FLAG_NO_FORMAT,
};
static struct fs_mount_t configuration_mount = {
	.type = FS_FATFS,
	.mnt_point = CONFIGURATION_MOUNT_POINT,
	.fs_data = &configuration_fat_fs,
	.storage_dev = (void *)PARTITION_ID(qspi_configuration_partition),
	.flags = FS_MOUNT_FLAG_NO_FORMAT,
};
static bool storage_mounted;
static bool configuration_mounted;
static bool storage_msc_active;
static int storage_msc_stop(void);
static const char *const msc_only_blocklist[] = {
	"cdc_acm_0",
	NULL,
};
static const char *const cdc_only_blocklist[] = {
	"msc_0",
	NULL,
};

USBD_DEVICE_DEFINE(hwdebug_cdc_usbd, DEVICE_DT_GET(STORAGE_USB_UDC_NODE),
	CONFIG_EF800_USB_VID, 0xef83);
USBD_DESC_LANG_DEFINE(hwdebug_cdc_lang);
USBD_DESC_MANUFACTURER_DEFINE(hwdebug_cdc_manufacturer, "EF800");
USBD_DESC_PRODUCT_DEFINE(hwdebug_cdc_product, "EF800 USB CDC Test");
USBD_DESC_CONFIG_DEFINE(hwdebug_cdc_config_desc, "CDC Test");
USBD_CONFIGURATION_DEFINE(hwdebug_cdc_config, 0, 125,
	&hwdebug_cdc_config_desc);
static struct usbd_context *cdc_usbd;
static const struct device *const cdc_uart =
	DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));
static bool cdc_configured;

static void cdc_usbd_message(struct usbd_context *ctx,
	const struct usbd_msg *msg)
{
	if (msg->type == USBD_MSG_VBUS_READY && usbd_can_detect_vbus(ctx)) {
		(void)usbd_enable((struct usbd_context *)ctx);
	}
}

void hwdebug_usb_cdc_test(void)
{
	uint8_t byte;
	uint32_t received = 0U;
	uint32_t echoed = 0U;
	int ret = 0;

	if (storage_msc_active) {
		hwdebug_console_puts("USB CDC test requires MSC to be stopped\r\n");
		return;
	}
	if (!device_is_ready(cdc_uart)) {
		hwdebug_console_puts("USB CDC UART is not ready\r\n");
		return;
	}
	if (!cdc_configured) {
		hwdebug_console_puts("USB CDC: add language descriptor\r\n");
		ret = usbd_add_descriptor(&hwdebug_cdc_usbd, &hwdebug_cdc_lang);
		if (ret == 0) {
			hwdebug_console_puts("USB CDC: add manufacturer descriptor\r\n");
			ret = usbd_add_descriptor(&hwdebug_cdc_usbd, &hwdebug_cdc_manufacturer);
		}
		if (ret == 0) {
			hwdebug_console_puts("USB CDC: add product descriptor\r\n");
			ret = usbd_add_descriptor(&hwdebug_cdc_usbd, &hwdebug_cdc_product);
		}
		if (ret == 0) {
			hwdebug_console_puts("USB CDC: add configuration\r\n");
			ret = usbd_add_configuration(&hwdebug_cdc_usbd, USBD_SPEED_FS,
				&hwdebug_cdc_config);
		}
		if (ret == 0) {
			cdc_configured = true;
		}
	}
	if (ret == 0) {
		hwdebug_console_puts("USB CDC: register classes\r\n");
		ret = usbd_register_all_classes(&hwdebug_cdc_usbd, USBD_SPEED_FS, 1,
			cdc_only_blocklist);
	}
	if (ret == 0) {
		hwdebug_console_puts("USB CDC: register message callback\r\n");
		ret = usbd_msg_register_cb(&hwdebug_cdc_usbd, cdc_usbd_message);
	}
	if (ret == 0) {
		hwdebug_console_puts("USB CDC: initialize controller\r\n");
		ret = usbd_init(&hwdebug_cdc_usbd);
	}
	if (ret != 0) {
		hwdebug_console_printf("USB CDC setup failed=%d\r\n", ret);
		return;
	}
	cdc_usbd = &hwdebug_cdc_usbd;
	hwdebug_console_printf("USB CDC: VBUS detection=%s\r\n",
		usbd_can_detect_vbus(cdc_usbd) ? "yes" : "no");
	ret = usbd_enable(cdc_usbd);
	if (ret != 0) {
		hwdebug_console_printf("USB CDC enable failed=%d\r\n", ret);
		return;
	}
	hwdebug_console_puts("USB CDC enabled; open the new COM port and send bytes\r\n"
		"Echo is active for 60 seconds; at least one byte is required\r\n");
	for (uint32_t elapsed = 0U; elapsed < CDC_TEST_DURATION_MS; elapsed += 10U) {
		while (uart_poll_in(cdc_uart, &byte) == 0) {
			received++;
			uart_poll_out(cdc_uart, byte);
			echoed++;
		}
		hwdebug_flash_watchdog_feed();
		k_msleep(10);
	}
	ret = usbd_disable(cdc_usbd);
	if (ret == 0) {
		ret = usbd_shutdown(cdc_usbd);
	}
	hwdebug_console_printf("USB CDC test %s: received=%u echoed=%u cleanup=%d\r\n",
		(received > 0U && received == echoed && ret == 0) ? "PASS" : "FAIL",
		(unsigned int)received, (unsigned int)echoed, ret);
}

static const struct gpio_dt_spec keyboard_reset =
	GPIO_DT_SPEC_GET(KEYBOARD_RESET_NODE, keyboard_reset_gpios);

int hwdebug_keyboard_reset_release(void)
{
	int ret;

	if (!gpio_is_ready_dt(&keyboard_reset)) {
		return -ENODEV;
	}
	ret = gpio_pin_configure_dt(&keyboard_reset, GPIO_OUTPUT_INACTIVE);
	if (ret == 0) {
		ret = gpio_pin_set_dt(&keyboard_reset, 1);
	}
	return ret;
}

enum interactive_test {
	INTERACTIVE_NONE,
	INTERACTIVE_KEYBOARD,
	INTERACTIVE_DISPLAY,
	INTERACTIVE_STORAGE,
	INTERACTIVE_QSPI_FORMAT,
	INTERACTIVE_QSPI_VOLUMES,
};

static enum interactive_test interactive_waiting;
static unsigned int storage_step;
static UART_HandleTypeDef lpuart1_hal_uart;
static bool keyboard_hal_active;
static int lpuart1_hal_init(void);
static int keyboard_command(const struct device *uart, uint8_t command,
				    const uint8_t *payload, size_t payload_length,
				    const uint8_t *response, size_t response_length);
static void uart_drain(const struct device *uart);
static int keyboard_reset_and_wait(const struct device *uart);
static int keyboard_response(const struct device *uart, uint8_t command,
				     const uint8_t *response, size_t length);
static int keyboard_receive_byte(const struct device *uart, uint8_t *byte,
					uint32_t timeout_ms);

static int keyboard_wait_initialized(const struct device *uart,
					     uint32_t timeout_ms)
{
	uint8_t byte;
	int ret = keyboard_receive_byte(uart, &byte, timeout_ms);

	if (ret == 0 && byte != CMD_INITIALIZED) {
		ret = -EPROTO;
	}
	return ret;
}
static const struct device *uart_for_name(const char *name)
{
	if (name != NULL && strcmp(name, "port1") == 0) {
		return DEVICE_DT_GET(DT_NODELABEL(usart3));
	}
	if (name != NULL && strcmp(name, "port2") == 0) {
		return DEVICE_DT_GET(DT_NODELABEL(usart1));
	}
	return NULL;
}

static int configure_uart(const struct device *uart)
{
	const struct uart_config config = {
		.baudrate = KB_UART_BAUDRATE,
		.parity = UART_CFG_PARITY_ODD,
		.stop_bits = UART_CFG_STOP_BITS_2,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};

	if (uart == NULL || !device_is_ready(uart)) {
		return -ENODEV;
	}
	return uart_configure(uart, &config);
}

static int configure_keyboard_uart(const struct device *uart)
{
	const struct uart_config config = {
		.baudrate = KB_UART_BAUDRATE,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};

	if (lpuart1_hal_init() == 0) {
		keyboard_hal_active = true;
		return 0;
	}
	keyboard_hal_active = false;
	if (uart == NULL || !device_is_ready(uart)) return -ENODEV;
	return uart_configure(uart, &config);
}

static int keyboard_hal_baudrate(uint32_t baudrate)
{
	if (!keyboard_hal_active) return -ENODEV;
	lpuart1_hal_uart.Init.BaudRate = baudrate;
	return HAL_UART_Init(&lpuart1_hal_uart) == HAL_OK ? 0 : -EIO;
}

static int storage_sync(void)
{
	return disk_access_ioctl(STORAGE_DISK_NAME, DISK_IOCTL_CTRL_SYNC, NULL);
}

static int storage_write_file(const char *path, const char *content)
{
	struct fs_file_t file;
	ssize_t written;
	int ret;

	fs_file_t_init(&file);
	ret = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE);
	if (ret != 0) {
		return ret;
	}
	written = fs_write(&file, content, strlen(content));
	ret = written == (ssize_t)strlen(content) ? 0 : -EIO;
	if (fs_close(&file) != 0 && ret == 0) {
		ret = -EIO;
	}
	return ret;
}

static int storage_read_file(const char *path, const char *expected)
{
	struct fs_file_t file;
	char buffer[64];
	ssize_t count;
	int ret;

	fs_file_t_init(&file);
	ret = fs_open(&file, path, FS_O_READ);
	if (ret != 0) {
		return ret;
	}
	count = fs_read(&file, buffer, sizeof(buffer) - 1U);
	if (count < 0) {
		ret = (int)count;
	} else {
		buffer[count] = '\0';
		ret = strcmp(buffer, expected) == 0 ? 0 : -EBADMSG;
	}
	if (fs_close(&file) != 0 && ret == 0) {
		ret = -EIO;
	}
	return ret;
}

static int storage_mount_fat(void)
{
	int ret;

	if (storage_mounted) {
		return 0;
	}
	ret = fs_mount(&storage_mount);
	if (ret == 0) {
		storage_mounted = true;
	}
	return ret;
}

int hwdebug_storage_prepare_update(void)
{
	int ret = storage_msc_stop();

	if (ret == 0) {
		ret = storage_sync();
	}
	if (ret == 0) {
		ret = storage_mount_fat();
	}
	return ret;
}

static int storage_unmount_fat(void)
{
	int ret;

	if (!storage_mounted) {
		return 0;
	}
	ret = fs_unmount(&storage_mount);
	if (ret == 0) {
		storage_mounted = false;
	}
	return ret;
}

static int storage_msc_start(void)
{
	int ret;
	const struct device *udc = DEVICE_DT_GET(STORAGE_USB_UDC_NODE);

	if (storage_msc_active) {
		return 0;
	}
	hwdebug_console_printf("storage: USB controller ready=%s\r\n",
		device_is_ready(udc) ? "yes" : "no");
	if (!device_is_ready(udc)) {
		return -ENODEV;
	}
	ret = disk_access_init(STORAGE_DISK_NAME);
	if (ret != 0) {
		return ret;
	}
	ret = usbd_add_descriptor(&hwdebug_usbd, &hwdebug_usb_lang);
	if (ret != 0) return ret;
	ret = usbd_add_descriptor(&hwdebug_usbd, &hwdebug_usb_manufacturer);
	if (ret != 0) return ret;
	ret = usbd_add_descriptor(&hwdebug_usbd, &hwdebug_usb_product);
	if (ret != 0) return ret;
	ret = usbd_add_configuration(&hwdebug_usbd, USBD_SPEED_FS,
		&hwdebug_usb_config);
	if (ret != 0) return ret;
	ret = usbd_register_all_classes(&hwdebug_usbd, USBD_SPEED_FS, 1,
		msc_only_blocklist);
	if (ret != 0) return ret;
	ret = usbd_init(&hwdebug_usbd);
	if (ret != 0) {
		hwdebug_console_printf("storage: USB init failed=%d\r\n", ret);
		return ret;
	}
	ret = usbd_enable(&hwdebug_usbd);
	if (ret != 0) {
		hwdebug_console_printf("storage: USB enable failed=%d\r\n", ret);
	}
	if (ret == 0) {
		storage_msc_active = true;
	}
	return ret;
}

static int storage_msc_stop(void)
{
	int ret;

	if (!storage_msc_active) {
		return 0;
	}
	ret = usbd_shutdown(&hwdebug_usbd);
	if (ret == 0) {
		storage_msc_active = false;
	}
	return ret;
}

static int storage_prepare_volume(void)
{
	int ret;
	const struct device *flash = DEVICE_DT_GET(STORAGE_FLASH_NODE);
	const struct flash_area *area;

	hwdebug_console_puts("storage: test implementation 3\r\n");
	ret = storage_msc_stop();
	if (ret != 0) return ret;
	hwdebug_console_printf("storage: qspi device ready=%s disk=%s\r\n",
		device_is_ready(flash) ? "yes" : "no", STORAGE_DISK_NAME);
	if (!device_is_ready(flash)) {
		return -ENODEV;
	}
	ret = disk_access_init(STORAGE_DISK_NAME);
	if (ret != 0) {
		hwdebug_console_printf("storage: disk init failed=%d status=%d\r\n",
			ret, disk_access_status(STORAGE_DISK_NAME));
		return ret;
	}
	hwdebug_console_printf("storage: disk ready status=%d\r\n",
		disk_access_status(STORAGE_DISK_NAME));
	hwdebug_console_puts("storage: synchronizing disk before erase\r\n");
	ret = storage_sync();
	if (ret != 0) {
		hwdebug_console_printf("storage: disk sync failed=%d\r\n", ret);
		return ret;
	}
	hwdebug_console_puts("storage: opening QSPI partition\r\n");
	ret = flash_area_open(PARTITION_ID(qspi_storage_partition), &area);
	if (ret != 0) {
		hwdebug_console_printf("storage: QSPI partition open failed=%d\r\n", ret);
		return ret;
	}
	hwdebug_console_printf("storage: erasing QSPI partition size=%u\r\n",
		(unsigned int)area->fa_size);
	ret = flash_area_erase(area, 0, area->fa_size);
	flash_area_close(area);
	if (ret != 0) {
		hwdebug_console_printf("storage: QSPI partition erase failed=%d\r\n", ret);
		return ret;
	}
	hwdebug_console_puts("storage: QSPI erase complete; mounting FAT\r\n");
	/* Let the FAT mount create a fresh filesystem using /EF800:. */
	storage_mount.flags = 0;
	ret = storage_mount_fat();
	storage_mount.flags = FS_MOUNT_FLAG_NO_FORMAT;
	if (ret != 0) {
		hwdebug_console_printf("storage: FAT format/mount failed=%d\r\n", ret);
		return ret;
	}
	hwdebug_console_puts("storage: FAT mounted; creating test directory\r\n");
	ret = fs_mkdir(STORAGE_DIR);
	if (ret != 0 && ret != -EEXIST) {
		hwdebug_console_printf("storage: test directory failed=%d\r\n", ret);
		return ret;
	}
	ret = storage_write_file(STORAGE_FAT_FILE, STORAGE_FAT_CONTENT);
	if (ret != 0) {
		hwdebug_console_printf("storage: FAT test file failed=%d\r\n", ret);
		return ret;
	}
	ret = storage_write_file(STORAGE_DUMMY_FILE, STORAGE_DUMMY_CONTENT);
	if (ret != 0) {
		hwdebug_console_printf("storage: dummy test file failed=%d\r\n", ret);
	}
	return ret;
}

static int format_qspi_partition(uint8_t partition_id, off_t expected_offset,
	size_t expected_size, const char *name, bool erase)
{
	const struct flash_area *area;
	size_t erased = 0U;
	int ret = flash_area_open(partition_id, &area);

	if (ret != 0) {
		hwdebug_console_printf("format: open %s partition failed=%d\r\n",
			name, ret);
		return ret;
	}
	if (area->fa_off != expected_offset || area->fa_size != expected_size) {
		hwdebug_console_printf(
			"format: refusing unexpected %s map off=0x%08x size=0x%08x\r\n",
			name, (unsigned int)area->fa_off, (unsigned int)area->fa_size);
		flash_area_close(area);
		return -EINVAL;
	}
	hwdebug_console_printf("format: %s %s offset=0x%08x size=0x%08x\r\n",
		erase ? "erasing" : "checking", name,
		(unsigned int)area->fa_off, (unsigned int)area->fa_size);
	if (!erase) {
		hwdebug_console_printf("format: skipped raw erase for %s\r\n", name);
		flash_area_close(area);
		return 0;
	}
	ret = 0;
	while (erased < area->fa_size) {
		struct flash_pages_info page;
		off_t absolute_offset = area->fa_off + (off_t)erased;

		ret = flash_get_page_info_by_offs(area->fa_dev, absolute_offset, &page);
		if (ret != 0 || page.size == 0U || page.size > area->fa_size - erased ||
		    page.start_offset != (uint32_t)absolute_offset) {
			ret = ret != 0 ? ret : -EINVAL;
			break;
		}
		hwdebug_flash_watchdog_feed();
		ret = flash_area_erase(area, erased, page.size);
		if (ret != 0) {
			break;
		}
		erased += page.size;
		if ((erased & 0xFFFFU) == 0U || erased == area->fa_size) {
			hwdebug_console_printf("format: %s erased=%u/%u bytes\r\n",
				name, (unsigned int)erased, (unsigned int)area->fa_size);
		}
		hwdebug_flash_watchdog_feed();
	}
	flash_area_close(area);
	if (ret != 0) {
		hwdebug_console_printf("format: erase %s failed=%d\r\n", name, ret);
	}
	return ret;
}

static int write_composition_slot(const char *path,
	const uint8_t *record, size_t length)
{
	struct fs_file_t file;
	ssize_t written;
	int ret;

	fs_file_t_init(&file);
	ret = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret != 0) {
		return ret;
	}
	written = fs_write(&file, record, length);
	ret = written == (ssize_t)length ? fs_sync(&file) : -EIO;
	if (fs_close(&file) != 0 && ret == 0) {
		ret = -EIO;
	}
	return ret;
}

static int verify_composition_slot(const char *path,
	const struct ef800_composition_profile *expected)
{
	static uint8_t record[EF800_COMPOSITION_RECORD_MAX];
	static struct ef800_composition_profile decoded;
	struct fs_file_t file;
	ssize_t length;
	int ret;

	fs_file_t_init(&file);
	ret = fs_open(&file, path, FS_O_READ);
	if (ret != 0) {
		return ret;
	}
	length = fs_read(&file, record, sizeof(record));
	ret = length > 0 ? 0 : (length < 0 ? (int)length : -EIO);
	if (fs_close(&file) != 0 && ret == 0) {
		ret = -EIO;
	}
	if (ret != 0) {
		return ret;
	}
	ret = ef800_composition_profile_decode(record, (size_t)length, &decoded);
	if (ret != 0) {
		return ret;
	}
	return ef800_composition_profile_equal(expected, &decoded) ? 0 : -EBADMSG;
}

static int verify_fat_volume_boot_sector(const char *disk_name, off_t offset,
	const char *name)
{
	const struct device *flash = DEVICE_DT_GET(STORAGE_FLASH_NODE);
	uint16_t bytes_per_sector;
	int ret;

	ret = disk_access_read(disk_name, qspi_format_sector, 0U, 1U);
	if (ret != 0) {
		hwdebug_console_printf("format: disk read %s sector failed=%d\r\n",
			name, ret);
		return ret;
	}
	hwdebug_console_printf("format: %s disk sector bytes=%02x %02x %02x; ",
		name, qspi_format_sector[0], qspi_format_sector[1],
		qspi_format_sector[2]);
	ret = flash_read(flash, offset, qspi_format_raw_sector,
		sizeof(qspi_format_raw_sector));
	if (ret != 0) {
		hwdebug_console_printf("format: read %s boot sector failed=%d\r\n",
			name, ret);
		return ret;
	}
	hwdebug_console_printf("raw sector bytes=%02x %02x %02x\r\n",
		qspi_format_raw_sector[0], qspi_format_raw_sector[1],
		qspi_format_raw_sector[2]);
	bytes_per_sector = (uint16_t)qspi_format_raw_sector[11] |
		((uint16_t)qspi_format_raw_sector[12] << 8U);
	if (!((qspi_format_raw_sector[0] == 0xE9U) ||
	      (qspi_format_raw_sector[0] == 0xEBU && qspi_format_raw_sector[2] == 0x90U)) ||
	    bytes_per_sector != QSPI_FS_SECTOR_SIZE ||
	    qspi_format_raw_sector[510] != 0x55U || qspi_format_raw_sector[511] != 0xAAU) {
		hwdebug_console_printf("format: %s FAT boot signature/sector size invalid\r\n",
			name);
		return -EBADMSG;
	}
	hwdebug_console_printf("format: %s FAT boot sector verified at 0x%08x\r\n",
		name, (unsigned int)offset);
	return 0;
}

static int qspi_format_current_layout(bool erase)
{
	qspi_format_profile = (struct ef800_composition_profile){
		.generation = 1U,
		.token_count = 2U,
	};
	size_t record_length = 0U;
	int ret;

	strcpy(qspi_format_profile.tokens[0], "BATSOC0");
	strcpy(qspi_format_profile.tokens[1], "LCDCONTR1");
	ret = storage_msc_stop();
	if (ret != 0) {
		return ret;
	}
	if (storage_mounted) {
		ret = storage_unmount_fat();
		if (ret != 0) {
			return ret;
		}
	}
	if (configuration_mounted) {
		ret = fs_unmount(&configuration_mount);
		if (ret != 0) {
			return ret;
		}
		configuration_mounted = false;
	}
	ret = disk_access_init(CONFIGURATION_DISK_NAME);
	if (ret == 0) {
		ret = disk_access_init(STORAGE_DISK_NAME);
	}
	if (ret == 0) {
		ret = disk_access_ioctl(CONFIGURATION_DISK_NAME,
			DISK_IOCTL_CTRL_SYNC, NULL);
	}
	if (ret == 0) {
		ret = disk_access_ioctl(STORAGE_DISK_NAME, DISK_IOCTL_CTRL_SYNC, NULL);
	}
	if (ret != 0) {
		hwdebug_console_printf("format: QSPI disk preparation failed=%d\r\n", ret);
		return ret;
	}

	/* Guard the destructive operation against any change to the expected map. */
	ret = format_qspi_partition(PARTITION_ID(qspi_bootstate_partition),
		0x000000U, 0x008000U, "boot-state NVS", erase);
	if (ret == 0) {
		ret = format_qspi_partition(PARTITION_ID(qspi_configuration_partition),
			0x008000U, 0x0F8000U, "Configuration FAT", erase);
	}
	if (ret == 0) {
		ret = format_qspi_partition(PARTITION_ID(qspi_storage_partition),
			0x100000U, 0x300000U, "Shared Data FAT", erase);
	}
	if (ret != 0) {
		return ret;
	}
	ret = fs_mkfs(FS_FATFS,
		(uintptr_t)(CONFIGURATION_DISK_NAME ":"), NULL, 0);
	hwdebug_console_printf("format: fs_mkfs %s ret=%d\r\n",
		CONFIGURATION_DISK_NAME, ret);
	if (ret == 0) {
		ret = disk_access_ioctl(CONFIGURATION_DISK_NAME,
			DISK_IOCTL_CTRL_SYNC, NULL);
	}
	if (ret == 0) {
		ret = fs_mkfs(FS_FATFS,
			(uintptr_t)(STORAGE_DISK_NAME ":"), NULL, 0);
		hwdebug_console_printf("format: fs_mkfs %s ret=%d\r\n",
			STORAGE_DISK_NAME, ret);
	}
	if (ret == 0) {
		ret = disk_access_ioctl(STORAGE_DISK_NAME, DISK_IOCTL_CTRL_SYNC, NULL);
	}
	if (ret == 0) {
		ret = verify_fat_volume_boot_sector(CONFIGURATION_DISK_NAME,
			0x00008000, "Configuration");
	}
	if (ret == 0) {
		ret = verify_fat_volume_boot_sector(STORAGE_DISK_NAME,
			0x00100000, "Shared Data");
	}
	if (ret != 0) {
		hwdebug_console_printf("format: FAT creation/sync/verification failed=%d\r\n",
			ret);
		return ret;
	}
	ret = fs_mount(&configuration_mount);
	if (ret != 0) {
		hwdebug_console_printf("format: Configuration mount failed=%d\r\n", ret);
		return ret;
	}
	configuration_mounted = true;
	ret = fs_mount(&storage_mount);
	if (ret != 0) {
		hwdebug_console_printf("format: Shared Data mount failed=%d\r\n", ret);
		goto unmount_configuration;
	}
	storage_mounted = true;
	ret = fs_mkdir(CONFIGURATION_PROFILE_DIRECTORY);
	if (ret != 0 && ret != -EEXIST) {
		hwdebug_console_printf("format: profile directory failed=%d\r\n", ret);
		goto unmount_volumes;
	}
	ret = ef800_composition_profile_encode(&qspi_format_profile,
		(char *)qspi_format_record, sizeof(qspi_format_record), &record_length);
	if (ret == 0) {
		ret = write_composition_slot(CONFIGURATION_PROFILE_A, qspi_format_record,
			record_length);
	}
	if (ret == 0) {
		ret = verify_composition_slot(CONFIGURATION_PROFILE_A,
			&qspi_format_profile);
	}
	if (ret == 0) {
		qspi_format_profile.generation = 2U;
		ret = ef800_composition_profile_encode(&qspi_format_profile,
			(char *)qspi_format_record, sizeof(qspi_format_record),
			&record_length);
	}
	if (ret == 0) {
		ret = write_composition_slot(CONFIGURATION_PROFILE_B,
			qspi_format_record,
			record_length);
	}
	if (ret == 0) {
		ret = verify_composition_slot(CONFIGURATION_PROFILE_B,
			&qspi_format_profile);
	}
	if (ret != 0) {
		hwdebug_console_printf("format: COMPOSITION initialization failed=%d\r\n",
			ret);
		goto unmount_volumes;
	}
	ret = disk_access_ioctl(CONFIGURATION_DISK_NAME, DISK_IOCTL_CTRL_SYNC, NULL);
	if (ret == 0) {
		ret = disk_access_ioctl(STORAGE_DISK_NAME, DISK_IOCTL_CTRL_SYNC, NULL);
	}
	if (ret != 0) {
		hwdebug_console_printf("format: final QSPI sync failed=%d\r\n", ret);
	}

unmount_volumes:
	if (storage_mounted) {
		int unmount_ret = fs_unmount(&storage_mount);

		if (ret == 0 && unmount_ret != 0) {
			ret = unmount_ret;
		} else if (unmount_ret == 0) {
			storage_mounted = false;
		}
	}
unmount_configuration:
	if (configuration_mounted) {
		int unmount_ret = fs_unmount(&configuration_mount);

		if (ret == 0 && unmount_ret != 0) {
			ret = unmount_ret;
		} else if (unmount_ret == 0) {
			configuration_mounted = false;
		}
	}
	return ret;
}

void hwdebug_storage_format_layout(void)
{
	storage_step = 0U;
	interactive_waiting = INTERACTIVE_QSPI_FORMAT;
	hwdebug_console_puts("WARNING: this erases all 4 MiB QSPI contents, including boot-state NVS and both FAT volumes.\r\n"
		"It then formats Configuration/Data and installs BATSOC0 + LCDCONTR1 in COMPOSITION.\r\n"
		"Type 'format-confirm' to continue, or 'cancel' to abort.\r\n");
}

void hwdebug_storage_format_volumes(void)
{
	storage_step = 0U;
	interactive_waiting = INTERACTIVE_QSPI_VOLUMES;
	hwdebug_console_puts("WARNING: this formats only the Configuration and Shared Data FAT volumes; it preserves boot-state NVS and does not erase the QSPI partitions.\r\n"
		"It installs BATSOC0 + LCDCONTR1 in COMPOSITION.\r\n"
		"Type 'format-confirm' to continue, or 'cancel' to abort.\r\n");
}

static int storage_internal_test(void)
{
	int ret = storage_read_file(STORAGE_FAT_FILE, STORAGE_FAT_CONTENT);
	if (ret == 0) {
		ret = storage_read_file(STORAGE_DUMMY_FILE, STORAGE_DUMMY_CONTENT);
	}
	return ret;
}

static int uart_receive(const struct device *uart, uint8_t expected,
				uint32_t timeout_ms)
{
	uint64_t deadline = k_uptime_get() + timeout_ms;
	uint8_t byte;

	while (k_uptime_get() < deadline) {
		if (keyboard_hal_active) {
			uint8_t received;
			if (HAL_UART_Receive(&lpuart1_hal_uart, &received, 1U, 10U) == HAL_OK) {
				return received == expected ? 0 : -EPROTO;
			}
			continue;
		}
		if (uart_poll_in(uart, &byte) == 0) {
			return byte == expected ? 0 : -EPROTO;
		}
		k_sleep(K_MSEC(1));
	}
	return -ETIMEDOUT;
}

static int keyboard_receive_byte(const struct device *uart, uint8_t *byte,
					uint32_t timeout_ms)
{
	uint64_t deadline = k_uptime_get() + timeout_ms;

	while (k_uptime_get() < deadline) {
		if (keyboard_hal_active) {
			if (HAL_UART_Receive(&lpuart1_hal_uart, byte, 1U, 10U) == HAL_OK) {
				return 0;
			}
			continue;
		}
		if (uart_poll_in(uart, byte) == 0) return 0;
		k_sleep(K_MSEC(1));
	}
	return -ETIMEDOUT;
}

static void keyboard_uart_send(const struct device *uart, uint8_t byte)
{
	if (keyboard_hal_active) {
		(void)HAL_UART_Transmit(&lpuart1_hal_uart, &byte, 1U,
			TEST_UART_TIMEOUT_MS);
	} else {
		uart_poll_out(uart, byte);
	}
}

void hwdebug_keyboard_baud_test(void)
{
	const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(lpuart1));
	int ret;

	hwdebug_test_begin("keyboard/baud-roundtrip");
	ret = configure_keyboard_uart(uart);
	if (ret == 0) ret = keyboard_reset_and_wait(uart);
	if (ret == 0) {
		uart_drain(uart);
		keyboard_uart_send(uart, CMD_KEYBOARD_BAUDRATE);
		keyboard_uart_send(uart, KB_BAUD_SELECTOR_9600);
		static const uint8_t baud_ack[] = {
			CMD_RETURN, CMD_KEYBOARD_BAUDRATE, CMD_ACK,
		};
		ret = keyboard_response(uart, CMD_KEYBOARD_BAUDRATE,
			baud_ack, sizeof(baud_ack));
		if (ret == 0) ret = keyboard_hal_baudrate(9600U);
	}
	if (ret == 0) {
		ret = keyboard_wait_initialized(uart, 500U);
		if (ret != 0) {
			(void)keyboard_hal_baudrate(KB_UART_BAUDRATE);
		}
	}
	if (ret == 0) {
		uart_drain(uart);
		keyboard_uart_send(uart, CMD_KEYBOARD_BAUDRATE);
		keyboard_uart_send(uart, KB_BAUD_SELECTOR_115200);
		static const uint8_t default_ack[] = {
			CMD_RETURN, CMD_KEYBOARD_BAUDRATE, CMD_ACK,
		};
		ret = keyboard_response(uart, CMD_KEYBOARD_BAUDRATE,
			default_ack, sizeof(default_ack));
		if (ret == 0) ret = keyboard_hal_baudrate(KB_UART_BAUDRATE);
	}
	if (ret == 0) {
		ret = keyboard_wait_initialized(uart, 500U);
		if (ret != 0) {
			(void)keyboard_hal_baudrate(9600U);
		}
	}
	if (ret == 0) {
		static const uint8_t echo[] = { CMD_KEYBOARD_ECHO };
		ret = keyboard_command(uart, CMD_KEYBOARD_ECHO, NULL, 0U,
			echo, sizeof(echo));
	}
	if (ret == 0) {
		static const uint8_t id[] = { CMD_RETURN, CMD_KEYBOARD_ID, 0xABU, 0x83U };
		ret = keyboard_command(uart, CMD_KEYBOARD_ID, NULL, 0U,
			id, sizeof(id));
	}
	if (ret == 0) {
		static const uint8_t activate[] = {
			CMD_RETURN, CMD_KEYBOARD_ACTIVATE, CMD_ACK,
		};
		ret = keyboard_command(uart, CMD_KEYBOARD_ACTIVATE, NULL, 0U,
			activate, sizeof(activate));
	}
	if (ret == 0) {
		static const uint8_t deactivate[] = {
			CMD_RETURN, CMD_KEYBOARD_DEACTIVATE, CMD_ACK,
		};
		ret = keyboard_command(uart, CMD_KEYBOARD_DEACTIVATE, NULL, 0U,
			deactivate, sizeof(deactivate));
	}
	if (ret == 0) hwdebug_test_pass();
	else hwdebug_test_fail("keyboard baud roundtrip failed", ret);
}

static void uart_drain(const struct device *uart)
{
	uint8_t byte;

	while (uart_poll_in(uart, &byte) == 0) {
	}
}

static int keyboard_reset_and_wait(const struct device *uart)
{
	uint8_t initialized;
	int ret;

	if (!gpio_is_ready_dt(&keyboard_reset)) {
		hwdebug_console_puts("keyboard: reset GPIO not ready\r\n");
		return -ENODEV;
	}
	ret = gpio_pin_configure_dt(&keyboard_reset, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		return ret;
	}
	uart_drain(uart);
	k_msleep(10U);
	ret = gpio_pin_set_dt(&keyboard_reset, 1);
	if (ret != 0) {
		return ret;
	}
	initialized = CMD_INITIALIZED;
	return uart_receive(uart, initialized, TEST_UART_TIMEOUT_MS);
}

static int lpuart1_hal_init(void)
{
	GPIO_InitTypeDef tx_gpio = {
		.Pin = GPIO_PIN_0,
		.Mode = GPIO_MODE_AF_PP,
		.Pull = GPIO_PULLUP,
		.Speed = GPIO_SPEED_FREQ_VERY_HIGH,
		.Alternate = GPIO_AF8_LPUART1,
	};
	GPIO_InitTypeDef rx_gpio = {
		.Pin = GPIO_PIN_7,
		.Mode = GPIO_MODE_AF_PP,
		.Pull = GPIO_PULLUP,
		.Speed = GPIO_SPEED_FREQ_VERY_HIGH,
		.Alternate = GPIO_AF8_LPUART1,
	};

	__HAL_RCC_GPIOG_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();
	/* Use HSI16 for LPUART1 so 9600 baud is independent of APB1. */
	__HAL_RCC_HSI_ENABLE();
	while (__HAL_RCC_GET_FLAG(RCC_FLAG_HSIRDY) == RESET) {
	}
	MODIFY_REG(RCC->CCIPR, RCC_CCIPR_LPUART1SEL,
		RCC_LPUART1CLKSOURCE_HSI);
	__HAL_RCC_LPUART1_CLK_ENABLE();
	__HAL_RCC_LPUART1_FORCE_RESET();
	__HAL_RCC_LPUART1_RELEASE_RESET();
	HAL_GPIO_Init(GPIOC, &tx_gpio);
	HAL_GPIO_Init(GPIOG, &rx_gpio);

	lpuart1_hal_uart.Instance = LPUART1;
	lpuart1_hal_uart.Init.BaudRate = KB_UART_BAUDRATE;
	lpuart1_hal_uart.Init.WordLength = UART_WORDLENGTH_8B;
	lpuart1_hal_uart.Init.StopBits = UART_STOPBITS_1;
	lpuart1_hal_uart.Init.Parity = UART_PARITY_NONE;
	lpuart1_hal_uart.Init.Mode = UART_MODE_TX_RX;
	lpuart1_hal_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	lpuart1_hal_uart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
	lpuart1_hal_uart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_SWAP_INIT;
	lpuart1_hal_uart.AdvancedInit.Swap = UART_ADVFEATURE_SWAP_ENABLE;

	return HAL_UART_Init(&lpuart1_hal_uart) == HAL_OK ? 0 : -EIO;
}

static void lpuart1_hal_print_registers(void)
{
	hwdebug_console_printf("rcc: AHB2ENR=0x%08x APB1ENR1=0x%08x CCIPR=0x%08x\r\n",
		(unsigned int)RCC->AHB2ENR, (unsigned int)RCC->APB1ENR1,
		(unsigned int)RCC->CCIPR);
	hwdebug_console_printf("rcc: APB1ENR2=0x%08x APB1RSTR2=0x%08x LPUART1EN=%u\r\n",
		(unsigned int)RCC->APB1ENR2, (unsigned int)RCC->APB1RSTR2,
		(unsigned int)((RCC->APB1ENR2 & RCC_APB1ENR2_LPUART1EN) != 0U));
	hwdebug_console_printf("gpioc: MODER=0x%08x PUPDR=0x%08x AFR0=0x%08x (LPUART1 TX=PC0)\r\n",
		(unsigned int)GPIOC->MODER, (unsigned int)GPIOC->PUPDR,
		(unsigned int)GPIOC->AFR[0]);
	hwdebug_console_printf("gpiog: MODER=0x%08x PUPDR=0x%08x AFR0=0x%08x AFR1=0x%08x (LPUART1 RX=PG7)\r\n",
		(unsigned int)GPIOG->MODER, (unsigned int)GPIOG->PUPDR,
		(unsigned int)GPIOG->AFR[0], (unsigned int)GPIOG->AFR[1]);
	hwdebug_console_printf("lpuart1: CR1=0x%08x CR2=0x%08x CR3=0x%08x BRR=0x%08x ISR=0x%08x\r\n",
		(unsigned int)LPUART1->CR1, (unsigned int)LPUART1->CR2,
		(unsigned int)LPUART1->CR3, (unsigned int)LPUART1->BRR,
		(unsigned int)LPUART1->ISR);
}

void hwdebug_lpuart_test(void)
{
	uint8_t received = 0U;
	int reset_configure;
	int reset_assert;
	int reset_release;
	int ret;

	hwdebug_test_begin("lpuart1/stm32-hal");
	ret = lpuart1_hal_init();
	lpuart1_hal_print_registers();
	if (ret != 0) {
		hwdebug_test_fail("STM32 HAL LPUART1 initialization failed", ret);
		return;
	}

	reset_configure = gpio_pin_configure_dt(&keyboard_reset, GPIO_OUTPUT_INACTIVE);
	reset_assert = reset_configure == 0 ? gpio_pin_set_dt(&keyboard_reset, 0) : reset_configure;
	k_msleep(10U);
	reset_release = reset_configure == 0 ? gpio_pin_set_dt(&keyboard_reset, 1) : reset_configure;
	hwdebug_console_printf("keyboard reset: configure=%d assert=%d release=%d\r\n",
		reset_configure, reset_assert, reset_release);
	if (reset_configure != 0 || reset_assert != 0 || reset_release != 0) {
		hwdebug_test_fail("keyboard reset GPIO sequence failed", -EIO);
		return;
	}

	/* Enter the blocking receive immediately after reset release. */
	ret = HAL_UART_Receive(&lpuart1_hal_uart, &received, 1U,
		TEST_UART_TIMEOUT_MS);
	hwdebug_console_printf("receive: status=%u byte=0x%02x uart-error=0x%08x\r\n",
		(unsigned int)ret, (unsigned int)received,
		(unsigned int)HAL_UART_GetError(&lpuart1_hal_uart));
	if (ret != HAL_OK) {
		hwdebug_test_fail("STM32 HAL LPUART1 receive failed", -ETIMEDOUT);
		return;
	}
	if (received != CMD_INITIALIZED) {
		hwdebug_test_fail("unexpected keyboard startup byte", -EPROTO);
		return;
	}
	hwdebug_test_pass();
}

void hwdebug_lpuart_host_test(void)
{
	static const uint8_t pattern[] = { 0x55U, 0xAAU, 0x00U, 0xFFU, 0x12U, 0x34U };
	uint8_t response;
	int reset_configure;
	int reset_assert;
	int ret;

	hwdebug_test_begin("lpuart1/host-echo");
	ret = lpuart1_hal_init();
	lpuart1_hal_print_registers();
	if (ret != 0) {
		hwdebug_test_fail("STM32 HAL LPUART1 initialization failed", ret);
		return;
	}

	reset_configure = gpio_pin_configure_dt(&keyboard_reset, GPIO_OUTPUT_INACTIVE);
	reset_assert = reset_configure == 0 ? gpio_pin_set_dt(&keyboard_reset, 0) : reset_configure;
	hwdebug_console_printf("keyboard reset held: configure=%d assert=%d\r\n",
		reset_configure, reset_assert);
	if (reset_configure != 0 || reset_assert != 0) {
		hwdebug_test_fail("keyboard reset hold failed", -EIO);
		return;
	}

	hwdebug_console_puts("host: echo the six bytes on COM10 at 115200 8N1\r\n");
	hwdebug_console_puts("host: 55 aa 00 ff 12 34\r\n");
	for (size_t index = 0U; index < sizeof(pattern); ++index) {
		if (HAL_UART_Transmit(&lpuart1_hal_uart, &pattern[index], 1U,
				TEST_UART_TIMEOUT_MS) != HAL_OK) {
			hwdebug_test_fail("STM32 HAL LPUART1 transmit failed", -EIO);
			return;
		}
		ret = HAL_UART_Receive(&lpuart1_hal_uart, &response, 1U,
			TEST_UART_TIMEOUT_MS);
		if (ret != HAL_OK) {
			hwdebug_console_printf("host: receive[%u] tx=0x%02x status=%u uart-error=0x%08x\r\n",
				(unsigned int)index, (unsigned int)pattern[index],
				(unsigned int)ret,
				(unsigned int)HAL_UART_GetError(&lpuart1_hal_uart));
			hwdebug_test_fail("STM32 HAL LPUART1 host receive failed", -ETIMEDOUT);
			return;
		}
		if (response != pattern[index]) {
			hwdebug_console_printf("host: echo[%u] expected=0x%02x received=0x%02x\r\n",
				(unsigned int)index, (unsigned int)pattern[index],
				(unsigned int)response);
			hwdebug_test_fail("LPUART1 host echo mismatch", -EPROTO);
			return;
		}
		hwdebug_console_printf("host: echo[%u]=0x%02x\r\n", (unsigned int)index,
			(unsigned int)response);
	}
	hwdebug_console_puts("host: all echo bytes valid uart-error=0x00000000\r\n");
	hwdebug_test_pass();
}

static void lpuart_bitbang_bit(const struct gpio_dt_spec *tx, int level)
{
	(void)gpio_pin_set_dt(tx, level);
	k_busy_wait(9U);
}

static void lpuart_bitbang_byte(const struct gpio_dt_spec *tx, uint8_t value)
{
	lpuart_bitbang_bit(tx, 0);
	for (unsigned int bit = 0U; bit < 8U; ++bit) {
		lpuart_bitbang_bit(tx, (value >> bit) & 1U);
	}
	lpuart_bitbang_bit(tx, 1);
}

void hwdebug_lpuart_bitbang_test(void)
{
	static const uint8_t pattern[] = { 0x55U, 0xAAU, 0x00U, 0xFFU, 0x34U };
	uint8_t response[sizeof(pattern)] = { 0U };
	const struct gpio_dt_spec tx = {
		.port = DEVICE_DT_GET(DT_NODELABEL(gpioc)),
		.pin = 0U,
		.dt_flags = 0U,
	};
	GPIO_InitTypeDef rx_gpio = {
		.Pin = GPIO_PIN_7,
		.Mode = GPIO_MODE_AF_PP,
		.Pull = GPIO_PULLUP,
		.Speed = GPIO_SPEED_FREQ_VERY_HIGH,
		.Alternate = GPIO_AF8_LPUART1,
	};
	int reset_configure;
	int reset_assert;
	int ret;

	hwdebug_test_begin("lpuart1/bitbang-echo");
	ret = lpuart1_hal_init();
	if (ret != 0) {
		hwdebug_test_fail("STM32 HAL LPUART1 RX initialization failed", ret);
		return;
	}
	reset_configure = gpio_pin_configure_dt(&keyboard_reset, GPIO_OUTPUT_INACTIVE);
	reset_assert = reset_configure == 0 ? gpio_pin_set_dt(&keyboard_reset, 0) : reset_configure;
	if (reset_configure != 0 || reset_assert != 0) {
		hwdebug_test_fail("keyboard reset hold failed", -EIO);
		return;
	}

	__HAL_RCC_GPIOG_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();
	HAL_GPIO_Init(GPIOG, &rx_gpio);
	ret = gpio_pin_configure(tx.port, tx.pin, GPIO_OUTPUT_HIGH);
	if (ret != 0) {
		hwdebug_test_fail("bitbang TX GPIO configuration failed", ret);
		return;
	}

	hwdebug_console_puts("bitbang: echo 55 aa 00 ff 34 on COM10 at 115200 8N1\r\n");
	for (size_t index = 0U; index < sizeof(pattern); ++index) {
		lpuart_bitbang_byte(&tx, pattern[index]);
	}
	(void)gpio_pin_set_dt(&tx, 1);

	for (size_t index = 0U; index < sizeof(response); ++index) {
		ret = HAL_UART_Receive(&lpuart1_hal_uart, &response[index], 1U,
			TEST_UART_TIMEOUT_MS);
		if (ret != HAL_OK) {
			hwdebug_console_printf("bitbang: receive[%u] status=%u uart-error=0x%08x\r\n",
				(unsigned int)index, (unsigned int)ret,
				(unsigned int)HAL_UART_GetError(&lpuart1_hal_uart));
			hwdebug_test_fail("bitbang host receive failed", -ETIMEDOUT);
			return;
		}
	}

	hwdebug_console_puts("bitbang: rx");
	for (size_t index = 0U; index < sizeof(response); ++index) {
		hwdebug_console_printf(" %02x", (unsigned int)response[index]);
		if (response[index] != pattern[index]) {
			hwdebug_console_puts("\r\n");
			hwdebug_test_fail("bitbang host echo mismatch", -EPROTO);
			return;
		}
	}
	hwdebug_console_puts("\r\n");
	hwdebug_test_pass();
}

static int keyboard_response(const struct device *uart, uint8_t command,
				     const uint8_t *response, size_t length)
{
	for (size_t index = 0U; index < length; ++index) {
		uint8_t received = 0U;
		int ret = keyboard_receive_byte(uart, &received, TEST_UART_TIMEOUT_MS);
		if (ret == 0 && received != response[index]) ret = -EPROTO;

		if (ret != 0) {
			hwdebug_console_printf("keyboard response command=0x%02x index=%u expected=0x%02x received=0x%02x error=%d\r\n",
				command, (unsigned int)index, response[index], received, ret);
			return ret;
		}
	}
	return 0;
}

void hwdebug_keyboard_version(void)
{
	const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(lpuart1));
	uint8_t response[5] = {0};
	int ret;

	hwdebug_test_begin("keyboard/version");
	ret = configure_keyboard_uart(uart);
	if (ret != 0) {
		hwdebug_console_printf("keyboard version: UART configure failed=%d ready=%s\r\n",
			ret, device_is_ready(uart) ? "yes" : "no");
	}
	if (ret == 0) ret = keyboard_reset_and_wait(uart);
	if (ret == 0) {
		uart_drain(uart);
		keyboard_uart_send(uart, CMD_KEYBOARD_VERSION);
		for (size_t index = 0U; index < sizeof(response); ++index) {
			ret = uart_receive(uart, response[index], TEST_UART_TIMEOUT_MS);
			if (ret != 0) break;
		}
	}
	if (ret == 0 && response[0] == CMD_RETURN &&
		response[1] == CMD_KEYBOARD_VERSION) {
		hwdebug_console_printf("keyboard version: %u.%u.%u\r\n",
			response[2], response[3], response[4]);
		hwdebug_test_pass();
	} else {
		hwdebug_test_fail("keyboard version response invalid", ret == 0 ? -EPROTO : ret);
	}
}

void hwdebug_keyboard_version_raw(void)
{
	const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(lpuart1));
	const uint8_t expected[] = {
		CMD_RETURN, CMD_KEYBOARD_VERSION, 1U, 1U, 1U,
	};
	int ret;

	hwdebug_test_begin("keyboard/version-raw");
	ret = configure_keyboard_uart(uart);
	if (ret == 0) ret = keyboard_reset_and_wait(uart);
	if (ret == 0) {
		uart_drain(uart);
		keyboard_uart_send(uart, CMD_KEYBOARD_VERSION);
		ret = keyboard_response(uart, CMD_KEYBOARD_VERSION, expected,
			sizeof(expected));
	}
	hwdebug_console_printf("keyboard version raw:");
	for (size_t index = 0U; index < sizeof(expected); ++index) {
		hwdebug_console_printf(" %02x", expected[index]);
	}
	hwdebug_console_puts("\r\n");
	if (ret == 0) hwdebug_test_pass();
	else hwdebug_test_fail("keyboard raw response receive failed", ret);
}

void hwdebug_keyboard_id_raw(void)
{
	const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(lpuart1));
	const uint8_t expected[] = {
		CMD_RETURN, CMD_KEYBOARD_ID, 0xABU, 0x83U,
	};
	int ret;

	hwdebug_test_begin("keyboard/id-raw");
	ret = configure_keyboard_uart(uart);
	if (ret == 0) ret = keyboard_reset_and_wait(uart);
	if (ret == 0) {
		uart_drain(uart);
		keyboard_uart_send(uart, CMD_KEYBOARD_ID);
		ret = keyboard_response(uart, CMD_KEYBOARD_ID, expected,
			sizeof(expected));
	}
	hwdebug_console_printf("keyboard id raw:");
	for (size_t index = 0U; index < sizeof(expected); ++index) {
		hwdebug_console_printf(" %02x", expected[index]);
	}
	hwdebug_console_puts("\r\n");
	if (ret == 0) hwdebug_test_pass();
	else hwdebug_test_fail("keyboard raw ID receive failed", ret);
}

static int keyboard_command(const struct device *uart, uint8_t command,
				    const uint8_t *payload, size_t payload_length,
				    const uint8_t *response, size_t response_length)
{
	uart_drain(uart);
	keyboard_uart_send(uart, command);
	for (size_t index = 0U; index < payload_length; ++index) {
		keyboard_uart_send(uart, payload[index]);
	}
	return keyboard_response(uart, command, response, response_length);
}

void hwdebug_keyboard_test(const char *port_name)
{
	const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(lpuart1));
	static const uint8_t ack_activate[] = { CMD_RETURN, CMD_KEYBOARD_ACTIVATE,
		CMD_ACK };
	static const uint8_t ack_deactivate[] = { CMD_RETURN, CMD_KEYBOARD_DEACTIVATE,
		CMD_ACK };
	static const uint8_t ack_typematic[] = { CMD_RETURN, CMD_KEYBOARD_TYPEMATIC,
		CMD_ACK, 0x2BU };
	static const uint8_t ack_debug[] = { CMD_RETURN, CMD_KEYBOARD_SET_DEBUG,
		CMD_ACK, 1U };
	static const uint8_t ack_debug_disable[] = { CMD_RETURN,
		CMD_KEYBOARD_SET_DEBUG, CMD_ACK, 0U };
	static const uint8_t id_response[] = { CMD_RETURN, CMD_KEYBOARD_ID,
		0xABU, 0x83U };
	static const uint8_t key_name_a[] = { KB_DEBUG_ACTION_PRESS, 1U, 'a' };
	static const uint8_t key_name_a_release[] = { KB_DEBUG_ACTION_RELEASE, 1U, 'a' };
	static const uint8_t make_a[] = { 0x1EU };
	static const uint8_t break_a[] = { KB_SCANCODE_BREAK, 0x1EU };
	int ret;
	ARG_UNUSED(port_name);

	hwdebug_test_begin("keyboard/protocol");
	ret = configure_keyboard_uart(uart);
	if (ret != 0) {
		hwdebug_console_printf("keyboard: UART configure failed=%d ready=%s\r\n",
			ret, device_is_ready(uart) ? "yes" : "no");
	}
	if (ret == 0) {
		ret = keyboard_reset_and_wait(uart);
	}
	if (ret == 0) {
		static const uint8_t echo[] = { CMD_KEYBOARD_ECHO };
		ret = keyboard_command(uart, CMD_KEYBOARD_ECHO, NULL, 0U,
					       echo, sizeof(echo));
	}
	if (ret == 0) {
		ret = keyboard_command(uart, CMD_KEYBOARD_ID, NULL, 0U,
					       id_response, sizeof(id_response));
	}
	if (ret == 0) {
		ret = keyboard_command(uart, CMD_KEYBOARD_ACTIVATE, NULL, 0U,
					       ack_activate, sizeof(ack_activate));
	}
	if (ret == 0) {
		static const uint8_t typematic[] = { 0x2BU };
		ret = keyboard_command(uart, CMD_KEYBOARD_TYPEMATIC, typematic,
					       sizeof(typematic), ack_typematic,
					       sizeof(ack_typematic));
	}
	if (ret == 0) {
		static const uint8_t enable[] = { 1U };
		ret = keyboard_command(uart, CMD_KEYBOARD_SET_DEBUG, enable,
					       sizeof(enable), ack_debug, sizeof(ack_debug));
	}
	if (ret == 0) {
		ret = keyboard_command(uart, CMD_KEYBOARD_DEBUG, key_name_a,
					       sizeof(key_name_a), make_a, sizeof(make_a));
	}
	if (ret == 0) {
		ret = keyboard_command(uart, CMD_KEYBOARD_DEBUG, key_name_a_release,
					       sizeof(key_name_a_release), break_a,
					       sizeof(break_a));
	}
	if (ret == 0) {
		static const uint8_t disable[] = { 0U };
		ret = keyboard_command(uart, CMD_KEYBOARD_SET_DEBUG, disable,
					       sizeof(disable), ack_debug_disable,
					       sizeof(ack_debug_disable));
	}
	if (ret == 0) {
		ret = keyboard_command(uart, CMD_KEYBOARD_DEACTIVATE, NULL, 0U,
					       ack_deactivate, sizeof(ack_deactivate));
	}
	if (ret != 0) {
		hwdebug_test_fail("keyboard startup or echo failed", ret);
	}
	hwdebug_test_pass();
}

void hwdebug_keyboard_interactive(void)
{
	hwdebug_console_puts("Keyboard interactive test: connect the controller and run the\r\n"
		"documented keyboard debug sequence from the MainApplication console.\r\n"
		"Reply with 'pass' or 'fail'. Use 'cancel' or 'exit' to leave the test.\r\n");
	interactive_waiting = INTERACTIVE_KEYBOARD;
}

static const struct ef800_t6963_config lcd_config = {
	.data = {
		GPIO_DT_SPEC_GET_BY_IDX(LCD_NODE, data_gpios, 0),
		GPIO_DT_SPEC_GET_BY_IDX(LCD_NODE, data_gpios, 1),
		GPIO_DT_SPEC_GET_BY_IDX(LCD_NODE, data_gpios, 2),
		GPIO_DT_SPEC_GET_BY_IDX(LCD_NODE, data_gpios, 3),
		GPIO_DT_SPEC_GET_BY_IDX(LCD_NODE, data_gpios, 4),
		GPIO_DT_SPEC_GET_BY_IDX(LCD_NODE, data_gpios, 5),
		GPIO_DT_SPEC_GET_BY_IDX(LCD_NODE, data_gpios, 6),
		GPIO_DT_SPEC_GET_BY_IDX(LCD_NODE, data_gpios, 7),
	},
	.cd = GPIO_DT_SPEC_GET(LCD_NODE, cd_gpios),
	.wr = GPIO_DT_SPEC_GET(LCD_NODE, wr_gpios),
	.rd = GPIO_DT_SPEC_GET(LCD_NODE, rd_gpios),
	.ce = GPIO_DT_SPEC_GET(LCD_NODE, ce_gpios),
	.fs = GPIO_DT_SPEC_GET(LCD_NODE, fs_gpios),
	.reset = GPIO_DT_SPEC_GET(LCD_NODE, reset_gpios),
	.pixels_horizontal = 240,
	.pixels_vertical = 64,
	.font_width = 6,
	.memory_kb = 8,
};

void hwdebug_display_test(void)
{
	struct ef800_t6963 lcd;
	static const char text[] = "EF800 LCD TEST";
	uint8_t readback[sizeof(text) - 1U];
	uint8_t expected[sizeof(text) - 1U];
	int ret;

	hwdebug_test_begin("display/t6963-write");
	ret = ef800_t6963_init(&lcd, &lcd_config);
	if (ret == 0) {
		ret = ef800_t6963_initialize(&lcd);
	}
	if (ret == 0) {
		ret = ef800_t6963_text_cursor(&lcd, 0U, 0U);
	}
	if (ret == 0) {
		ret = ef800_t6963_write_string(&lcd, text);
	}
	if (ret == 0) {
		ret = ef800_t6963_read_text(&lcd, 0U, 0U, readback,
					    sizeof(readback));
	}
	if (ret == 0) {
		for (size_t index = 0U; index < sizeof(expected); ++index) {
			expected[index] = (uint8_t)text[index] - ' ';
		}
		if (memcmp(readback, expected, sizeof(readback)) != 0) {
			ret = -EPROTO;
		}
	}
	if (ret == 0) {
		ret = ef800_t6963_create_line(&lcd, 0, 16, 239, 16);
	}
	if (ret == 0) {
		ret = ef800_t6963_create_line(&lcd, 0, 32, 239, 32);
	}
	if (ret != 0) {
		hwdebug_test_fail("T6963 write/readback test failed", ret);
	}
	hwdebug_test_pass();
}

void hwdebug_display_interactive(void)
{
	struct ef800_t6963 lcd;
	int ret;

	hwdebug_test_begin("display/interactive");
	ret = ef800_t6963_init(&lcd, &lcd_config);
	if (ret == 0) ret = ef800_t6963_initialize(&lcd);
	if (ret == 0) ret = ef800_t6963_clear(&lcd);
	if (ret == 0) ret = ef800_t6963_text_cursor(&lcd, 0U, 0U);
	if (ret == 0) ret = ef800_t6963_write_string(&lcd, "EF800 DISPLAY TEST");
	if (ret == 0) ret = ef800_t6963_create_line(&lcd, 0, 16, 239, 16);
	if (ret == 0) ret = ef800_t6963_create_line(&lcd, 0, 32, 239, 32);
	if (ret == 0) ret = ef800_t6963_create_line(&lcd, 0, 0, 239, 63);
	if (ret != 0) {
		hwdebug_test_fail("T6963 interactive display failed", ret);
		hwdebug_test_pass();
		return;
	}
	hwdebug_console_puts("Display test active: verify 'EF800 DISPLAY TEST' and the line pattern.\r\n"
		"Reply with 'pass' or 'fail'. Use 'cancel' or 'exit' to leave the test.\r\n");
	interactive_waiting = INTERACTIVE_DISPLAY;
}

void hwdebug_display_wr_test(uint32_t count)
{
	const struct gpio_dt_spec wr = GPIO_DT_SPEC_GET(LCD_NODE, wr_gpios);
	const struct gpio_dt_spec rd = GPIO_DT_SPEC_GET(LCD_NODE, rd_gpios);
	const struct gpio_dt_spec ce = GPIO_DT_SPEC_GET(LCD_NODE, ce_gpios);
	const struct gpio_dt_spec cd = GPIO_DT_SPEC_GET(LCD_NODE, cd_gpios);
	int ret;

	if (count == 0U || count > 1000U) {
		hwdebug_console_puts("error: WR pulse count must be 1..1000\r\n");
		return;
	}
	hwdebug_test_begin("display/wr-gpio");
	ret = gpio_pin_configure_dt(&wr, GPIO_OUTPUT);
	if (ret == 0) ret = gpio_pin_configure_dt(&rd, GPIO_OUTPUT);
	if (ret == 0) ret = gpio_pin_configure_dt(&ce, GPIO_OUTPUT);
	if (ret == 0) ret = gpio_pin_configure_dt(&cd, GPIO_OUTPUT);
	if (ret == 0) {
		(void)gpio_pin_set_dt(&rd, 1);
		(void)gpio_pin_set_dt(&ce, 1);
		(void)gpio_pin_set_dt(&cd, 1);
		(void)gpio_pin_set_dt(&wr, 1);
		for (uint32_t index = 0U; index < count; ++index) {
			(void)gpio_pin_set_dt(&wr, 0);
			k_busy_wait(1000U);
			(void)gpio_pin_set_dt(&wr, 1);
			k_busy_wait(1000U);
		}
	}
	(void)gpio_pin_set_dt(&wr, 1);
	(void)gpio_pin_set_dt(&rd, 1);
	(void)gpio_pin_set_dt(&ce, 1);
	if (ret != 0) hwdebug_test_fail("WR GPIO pulse test failed", ret);
	hwdebug_test_pass();
}

void hwdebug_display_status(void)
{
	const struct gpio_dt_spec *data[] = {
		&lcd_config.data[0], &lcd_config.data[1], &lcd_config.data[2],
		&lcd_config.data[3], &lcd_config.data[4], &lcd_config.data[5],
		&lcd_config.data[6], &lcd_config.data[7],
	};
	int ret = 0;

	hwdebug_console_puts("display status: raw T6963 status reads\r\n");
	for (size_t index = 0U; index < ARRAY_SIZE(data); ++index) {
		ret = gpio_pin_configure_dt(data[index], GPIO_INPUT);
		if (ret != 0) {
			hwdebug_console_printf("ERROR data[%u] configure=%d\r\n",
				(unsigned int)index, ret);
			return;
		}
	}
	ret = gpio_pin_configure_dt(&lcd_config.cd, GPIO_OUTPUT);
	if (ret == 0) ret = gpio_pin_configure_dt(&lcd_config.rd, GPIO_OUTPUT);
	if (ret == 0) ret = gpio_pin_configure_dt(&lcd_config.ce, GPIO_OUTPUT);
	if (ret == 0) ret = gpio_pin_configure_dt(&lcd_config.wr, GPIO_OUTPUT);
	if (ret == 0) ret = gpio_pin_configure_dt(&lcd_config.fs, GPIO_OUTPUT);
	if (ret == 0) ret = gpio_pin_configure_dt(&lcd_config.reset, GPIO_OUTPUT);
	if (ret != 0) {
		hwdebug_console_printf("ERROR control configure=%d\r\n", ret);
		return;
	}

	(void)gpio_pin_set_dt(&lcd_config.cd, 1);
	(void)gpio_pin_set_dt(&lcd_config.wr, 1);
	(void)gpio_pin_set_dt(&lcd_config.rd, 1);
	(void)gpio_pin_set_dt(&lcd_config.ce, 1);
	(void)gpio_pin_set_dt(&lcd_config.fs, 1);
	(void)gpio_pin_set_dt(&lcd_config.reset, 1);
	k_busy_wait(1000U);

	for (unsigned int sample = 0U; sample < 8U; ++sample) {
		uint8_t value = 0U;

		(void)gpio_pin_set_dt(&lcd_config.cd, 1);
		(void)gpio_pin_set_dt(&lcd_config.rd, 0);
		(void)gpio_pin_set_dt(&lcd_config.ce, 0);
		k_busy_wait(2U);
		for (size_t bit = 0U; bit < ARRAY_SIZE(data); ++bit) {
			int level = gpio_pin_get_dt(data[bit]);
			if (level < 0) {
				hwdebug_console_printf("ERROR status[%u] data[%u]=%d\r\n",
					sample, (unsigned int)bit, level);
				ret = level;
				break;
			}
			if (level != 0) value |= BIT(bit);
		}
		(void)gpio_pin_set_dt(&lcd_config.ce, 1);
		(void)gpio_pin_set_dt(&lcd_config.rd, 1);
		if (ret != 0) return;
		hwdebug_console_printf("status[%u]=0x%02x ready=%s\r\n", sample,
			value, (value & 0x03U) == 0x03U ? "yes" : "no");
		k_busy_wait(100U);
	}
}

void hwdebug_uart_test(const char *port_name)
{
	const struct device *uart = uart_for_name(port_name);
	int ret;

	hwdebug_test_begin(port_name != NULL && strcmp(port_name, "port1") == 0 ?
			   "uart3/rs232-loopback" : "uart1/rs232-loopback");
	ret = configure_uart(uart);
	if (ret == 0) {
		uart_poll_out(uart, 0x55U);
		ret = uart_receive(uart, 0x55U, TEST_UART_TIMEOUT_MS);
	}
	if (ret != 0) {
		hwdebug_test_fail("selected UART loopback failed", ret);
	}
	hwdebug_test_pass();
	hwdebug_console_puts("UART test uses 115200 8O2. Connect the PC RS232 adapter\r\n"
		"through the approved level converter and connect TX to RX for loopback.\r\n");
}

void hwdebug_storage_test(void)
{
	storage_step = 1U;
	interactive_waiting = INTERACTIVE_STORAGE;
	hwdebug_console_puts("WARNING: storage test formats the QSPI volume and deletes all files.\r\n"
		"Type 'format-confirm' to continue, or 'cancel' to abort.\r\n");
}

bool hwdebug_interactive_pending(void)
{
	return interactive_waiting != INTERACTIVE_NONE;
}

void hwdebug_interactive_reply(const char *reply)
{
	if (interactive_waiting == INTERACTIVE_NONE) {
		return;
	}
	if (interactive_waiting == INTERACTIVE_QSPI_FORMAT ||
	    interactive_waiting == INTERACTIVE_QSPI_VOLUMES) {
		if (strcmp(reply, "format-confirm") == 0) {
			bool erase = interactive_waiting == INTERACTIVE_QSPI_FORMAT;
			int ret = qspi_format_current_layout(erase);

			if (ret == 0) {
				hwdebug_console_puts("QSPI FORMAT PASS; Configuration/Data FAT and BATSOC0/LCDCONTR1 profile verified.\r\n");
			} else {
				hwdebug_console_printf("QSPI FORMAT FAIL: %d; do not power-cycle until media state is reviewed.\r\n",
					ret);
			}
			interactive_waiting = INTERACTIVE_NONE;
			return;
		}
		if (strcmp(reply, "cancel") == 0 || strcmp(reply, "exit") == 0) {
			hwdebug_console_puts("QSPI format cancelled; no media changes made.\r\n");
			interactive_waiting = INTERACTIVE_NONE;
			return;
		}
		hwdebug_console_puts("reply 'format-confirm' to proceed, or 'cancel' to abort\r\n");
		return;
	}
	if (interactive_waiting == INTERACTIVE_STORAGE && storage_step == 1U &&
		strcmp(reply, "format-confirm") == 0) {
		int ret = storage_prepare_volume();
		if (ret != 0) {
			hwdebug_console_printf("ERROR storage format/internal preparation failed (error=%d)\r\n", ret);
			hwdebug_console_puts("FAIL\r\n");
			interactive_waiting = INTERACTIVE_NONE;
			return;
		}
		ret = storage_internal_test();
		if (ret != 0) {
			hwdebug_console_printf("ERROR internal FAT readback failed (error=%d)\r\n", ret);
			hwdebug_console_puts("FAIL\r\n");
			interactive_waiting = INTERACTIVE_NONE;
			return;
		}
		storage_step = 2U;
		hwdebug_console_puts("RUN   storage/internal-fat\r\nPASS\r\n"
			"Switching ownership to USB MSC. Connect USB and confirm these files are visible:\r\n"
			"  storage_test/created_by_fat.txt\r\n"
			"  storage_test/dummy.txt\r\n"
		"Reply 'pass' or 'fail'.\r\n");
		ret = storage_unmount_fat();
		if (ret != 0 || storage_sync() != 0 || storage_msc_start() != 0) {
			hwdebug_console_puts("ERROR FAT-to-MSC ownership handoff failed\r\nFAIL\r\n");
			interactive_waiting = INTERACTIVE_NONE;
		}
		return;
	}
	if (interactive_waiting == INTERACTIVE_STORAGE && storage_step == 2U &&
		strcmp(reply, "pass") == 0) {
		storage_step = 3U;
		hwdebug_console_puts("Step 3/4: create storage_test/created_by_msc.txt from the USB host,\r\n"
			"write exactly: created by USB MSC, safely eject, then reply 'pass' or 'fail'.\r\n");
		return;
	}
	if (interactive_waiting == INTERACTIVE_STORAGE && storage_step == 3U &&
		strcmp(reply, "pass") == 0) {
		int ret = storage_msc_stop();
		if (ret == 0) ret = storage_sync();
		if (ret == 0) ret = storage_mount_fat();
		if (ret == 0) ret = storage_read_file(STORAGE_MSC_FILE, STORAGE_MSC_CONTENT);
		if (ret != 0) {
			hwdebug_console_printf("ERROR MSC-created file was not readable by FAT (error=%d)\r\n", ret);
			hwdebug_console_puts("FAIL\r\n");
			interactive_waiting = INTERACTIVE_NONE;
			return;
		}
		storage_step = 4U;
		hwdebug_console_puts("RUN   storage/msc-to-fat\r\nPASS\r\n"
		"Step 4/4: unmount and remount FAT, then reply 'pass' if all three files remain readable,\r\n"
		"otherwise reply 'fail'.\r\n");
		(void)storage_unmount_fat();
		if (storage_mount_fat() != 0) {
			hwdebug_console_puts("ERROR final FAT remount failed\r\nFAIL\r\n");
			interactive_waiting = INTERACTIVE_NONE;
		}
		return;
	}
	if (interactive_waiting == INTERACTIVE_STORAGE && storage_step == 4U &&
		strcmp(reply, "pass") == 0) {
		int ret = storage_internal_test();
		if (ret == 0) ret = storage_read_file(STORAGE_MSC_FILE, STORAGE_MSC_CONTENT);
		hwdebug_console_puts(ret == 0 ? "STORAGE TEST PASS\r\n" : "STORAGE TEST FAIL\r\n");
		interactive_waiting = INTERACTIVE_NONE;
		return;
	}
	if (interactive_waiting == INTERACTIVE_STORAGE && storage_step >= 2U &&
		storage_step <= 4U && strcmp(reply, "fail") == 0) {
		/* Do not remount FAT while the host may still own the MSC volume. */
		int ret = storage_msc_stop();
		hwdebug_console_printf("storage: MSC stopped after failure=%d; FAT left unmounted\r\n",
			ret);
		hwdebug_console_puts("STORAGE TEST FAIL\r\n");
		interactive_waiting = INTERACTIVE_NONE;
		return;
	}
	if (interactive_waiting == INTERACTIVE_STORAGE && strcmp(reply, "next") == 0) {
		++storage_step;
		switch (storage_step) {
		case 2U:
			hwdebug_console_puts("Step 2/5: assign Port 4 to MSD and connect USB VBUS.\r\n"
				"Reply 'next' when complete.\r\n");
			break;
		case 3U:
			hwdebug_console_puts("Step 3/5: confirm logs show FAT unmount before USB MSC\r\n"
				"enable. Reply 'next' when complete.\r\n");
			break;
		case 4U:
			hwdebug_console_puts("Step 4/5: confirm the host enumerates MSC and reads the\r\n"
				"same volume without formatting. Reply 'next' when complete.\r\n");
			break;
		case 5U:
			hwdebug_console_puts("Step 5/5: disconnect USB, reset, and confirm FAT mounts\r\n"
				"again. Reply 'pass' or 'fail' for the final result.\r\n");
			break;
		default:
			break;
		}
		return;
	}
	if (interactive_waiting == INTERACTIVE_STORAGE &&
		(strcmp(reply, "cancel") == 0 || strcmp(reply, "exit") == 0)) {
		if (storage_step == 2U) {
			(void)storage_msc_stop();
		} else if (storage_step >= 3U) {
			(void)storage_msc_stop();
			(void)storage_mount_fat();
		}
	}
	if (interactive_waiting == INTERACTIVE_STORAGE && storage_step != 5U &&
	    (strcmp(reply, "pass") == 0 || strcmp(reply, "fail") == 0)) {
		hwdebug_console_puts("reply 'next' to continue the storage checklist\r\n");
		return;
	}
	if (strcmp(reply, "pass") == 0) {
		hwdebug_console_puts("PASS\r\n");
		interactive_waiting = INTERACTIVE_NONE;
	} else if (strcmp(reply, "fail") == 0) {
		hwdebug_console_puts("FAIL\r\n");
		interactive_waiting = INTERACTIVE_NONE;
	} else if (strcmp(reply, "cancel") == 0 || strcmp(reply, "exit") == 0) {
		hwdebug_console_puts("interactive test cancelled\r\n");
		interactive_waiting = INTERACTIVE_NONE;
	}
}
