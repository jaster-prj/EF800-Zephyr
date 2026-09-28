# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ef800_hwdebug)

target_sources(app PRIVATE
	src/main.c
	src/test_result.c
	src/flash.c
	src/mcp23017.c
	src/rtc.c
	src/fuel_gauge.c
	src/peripheral_tests.c
	src/keyboard_update.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_drivers/ef800_mcp23017.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_drivers/ef800_max17043.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_drivers/ef800_t6963.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_update/ef800_update.c
	${ZEPHYR_HAL_STM32_MODULE_DIR}/stm32cube/stm32l4xx/drivers/src/stm32l4xx_hal_gpio.c
	${ZEPHYR_HAL_STM32_MODULE_DIR}/stm32cube/stm32l4xx/drivers/src/stm32l4xx_hal_i2c.c
	${ZEPHYR_HAL_STM32_MODULE_DIR}/stm32cube/stm32l4xx/drivers/src/stm32l4xx_hal_i2c_ex.c
	${ZEPHYR_HAL_STM32_MODULE_DIR}/stm32cube/stm32l4xx/drivers/src/stm32l4xx_hal_uart.c
	${ZEPHYR_HAL_STM32_MODULE_DIR}/stm32cube/stm32l4xx/drivers/src/stm32l4xx_hal_uart_ex.c
)

target_include_directories(app PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_drivers
	${CMAKE_CURRENT_LIST_DIR}/../../shared/keyboard_protocol
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_update
)
