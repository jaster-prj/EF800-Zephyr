/* SPDX-License-Identifier: Apache-2.0 */

#include "mcp23017.h"
#include "test_result.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>

#include <stm32l4xx_hal.h>
#include <stm32l4xx_hal_i2c.h>

#include "ef800_mcp23017.h"
#include "flash.h"

#define MCP23017_HAL_ADDRESS  (0x24U << 1)
#define MCP23017_HAL_TIMEOUT  100U
#define MCP23017_HAL_TIMING   0x00707CBBU
#define MCP23017_HAL_IODIR    0x00U
#define MCP23017_HAL_GPIO     0x12U

#if DT_HAS_COMPAT_STATUS_OKAY(microchip_mcp23017)
static const struct device *const mcp23017 =
	DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(microchip_mcp23017));
static const struct device *const mcp23017_i2c =
	DEVICE_DT_GET(DT_BUS(DT_COMPAT_GET_ANY_STATUS_OKAY(microchip_mcp23017)));
#endif

static I2C_HandleTypeDef mcp23017_hal_i2c;

static void mcp23017_print_registers(const char *phase, const char *test_name)
{
	hwdebug_console_printf("mcp23017/registers-%s-%s\r\n", phase, test_name);
	hwdebug_console_printf("rcc: AHB2ENR=0x%08x APB1ENR1=0x%08x CCIPR=0x%08x\r\n",
		(unsigned int)RCC->AHB2ENR, (unsigned int)RCC->APB1ENR1,
		(unsigned int)RCC->CCIPR);
	hwdebug_console_printf("gpiog: MODER=0x%08x OTYPER=0x%08x OSPEEDR=0x%08x PUPDR=0x%08x AFR1=0x%08x\r\n",
		(unsigned int)GPIOG->MODER, (unsigned int)GPIOG->OTYPER,
		(unsigned int)GPIOG->OSPEEDR, (unsigned int)GPIOG->PUPDR,
		(unsigned int)GPIOG->AFR[1]);
	hwdebug_console_printf("i2c1: CR1=0x%08x TIMINGR=0x%08x ISR=0x%08x\r\n",
		(unsigned int)I2C1->CR1, (unsigned int)I2C1->TIMINGR,
		(unsigned int)I2C1->ISR);
}

static int mcp23017_hal_init(void)
{
	GPIO_InitTypeDef gpio = {
		.Pin = GPIO_PIN_13 | GPIO_PIN_14,
		.Mode = GPIO_MODE_AF_OD,
		.Pull = GPIO_PULLUP,
		.Speed = GPIO_SPEED_FREQ_HIGH,
		.Alternate = GPIO_AF4_I2C1,
	};

	__HAL_RCC_GPIOG_CLK_ENABLE();
	__HAL_RCC_I2C1_CLK_ENABLE();
	__HAL_RCC_I2C1_FORCE_RESET();
	__HAL_RCC_I2C1_RELEASE_RESET();
	HAL_GPIO_Init(GPIOG, &gpio);

	mcp23017_hal_i2c.Instance = I2C1;
	mcp23017_hal_i2c.Init.Timing = MCP23017_HAL_TIMING;
	mcp23017_hal_i2c.Init.OwnAddress1 = 0U;
	mcp23017_hal_i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
	mcp23017_hal_i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
	mcp23017_hal_i2c.Init.OwnAddress2 = 0U;
	mcp23017_hal_i2c.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
	mcp23017_hal_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
	mcp23017_hal_i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

	return HAL_I2C_Init(&mcp23017_hal_i2c) == HAL_OK ? 0 : -EIO;
}

static int mcp23017_hal_read(uint16_t reg, uint8_t *data, uint16_t length)
{
	return HAL_I2C_Mem_Read(&mcp23017_hal_i2c, MCP23017_HAL_ADDRESS, reg,
		I2C_MEMADD_SIZE_8BIT, data, length, MCP23017_HAL_TIMEOUT) == HAL_OK ?
		0 : -EIO;
}

static int mcp23017_raw_read(uint8_t reg, uint8_t *data, size_t length)
{
	uint8_t register_address = reg;

	return i2c_write_read(mcp23017_i2c, 0x24, &register_address, 1U,
		data, length);
}

static int mcp23017_zephyr_prepare(void)
{
	return i2c_configure(mcp23017_i2c, I2C_SPEED_SET(I2C_SPEED_FAST));
}

void hwdebug_mcp23017_raw_test(void)
{
	hwdebug_test_begin("mcp23017/raw-probe");
	mcp23017_print_registers("before", "raw");
#if DT_HAS_COMPAT_STATUS_OKAY(microchip_mcp23017)
	uint8_t data[2];
	int ret;

	if (!device_is_ready(mcp23017_i2c)) {
		hwdebug_test_fail("I2C controller is not ready", -ENODEV);
	} else if ((ret = mcp23017_zephyr_prepare()) != 0) {
		hwdebug_test_fail("Zephyr I2C configure failed", ret);
	} else if ((ret = mcp23017_raw_read(0x00U, data, sizeof(data))) != 0) {
		hwdebug_test_fail("raw IODIR register read failed", ret);
	} else if ((ret = mcp23017_raw_read(0x12U, data, sizeof(data))) != 0) {
		hwdebug_test_fail("raw GPIO register read failed", ret);
	}
#else
	hwdebug_test_fail("MCP23017 is missing from devicetree", -ENODEV);
	#endif
	mcp23017_print_registers("after", "raw");
	hwdebug_test_pass();
}

void hwdebug_mcp23017_hal_test(void)
{
	hwdebug_test_begin("mcp23017/stm32-hal");
	mcp23017_print_registers("before", "stm32-hal");
#if DT_HAS_COMPAT_STATUS_OKAY(microchip_mcp23017)
	uint8_t data[2];
	int ret = mcp23017_hal_init();

	if (ret != 0) {
		hwdebug_test_fail("STM32 HAL I2C1 initialization failed", ret);
	} else if (HAL_I2C_IsDeviceReady(&mcp23017_hal_i2c,
		MCP23017_HAL_ADDRESS, 2U, MCP23017_HAL_TIMEOUT) != HAL_OK) {
		hwdebug_console_printf("ERROR STM32 HAL I2C device-ready failed: HAL error=0x%08x\r\n",
			(unsigned int)HAL_I2C_GetError(&mcp23017_hal_i2c));
		hwdebug_test_fail("STM32 HAL MCP23017 address did not ACK", -EIO);
	} else if (mcp23017_hal_read(MCP23017_HAL_IODIR, data, sizeof(data)) != 0) {
		hwdebug_test_fail("STM32 HAL IODIR register read failed", -EIO);
	} else if (mcp23017_hal_read(MCP23017_HAL_GPIO, data, sizeof(data)) != 0) {
		hwdebug_test_fail("STM32 HAL GPIO register read failed", -EIO);
	}
#else
	hwdebug_test_fail("MCP23017 HAL test requires the board I2C pins", -ENODEV);
	#endif
	mcp23017_print_registers("after", "stm32-hal");
	hwdebug_test_pass();
}

void hwdebug_mcp23017_driver_test(void)
{
	hwdebug_test_begin("mcp23017/shared-driver");
	mcp23017_print_registers("before", "shared-driver");
#if DT_HAS_COMPAT_STATUS_OKAY(microchip_mcp23017)
	uint16_t value;
	int ret = mcp23017_zephyr_prepare();

	if (ret != 0) {
		hwdebug_test_fail("Zephyr I2C configure failed", ret);
	} else if ((ret = ef800_mcp23017_probe(mcp23017, &value)) != 0) {
		hwdebug_test_fail(device_is_ready(mcp23017) ?
			"shared driver GPIO port read failed" : "MCP23017 device is not ready", ret);
	}
#else
	hwdebug_test_fail("MCP23017 is missing from devicetree", -ENODEV);
	#endif
	mcp23017_print_registers("after", "shared-driver");
	hwdebug_test_pass();
}

void hwdebug_mcp23017_test(void)
{
	hwdebug_mcp23017_raw_test();
	hwdebug_mcp23017_hal_test();
	hwdebug_mcp23017_driver_test();
}
