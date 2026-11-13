# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.28.0)

set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../boards/ef800_keyboard)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ef800_keyboard)

target_sources(app PRIVATE
	src/main.c
	src/keyboard_app_update.c
	src/keyboard_matrix.c
	src/keyboard_i2c_service.c
	src/keyboard_uart_protocol.c
	src/keyboard_key_names.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/keyboard_protocol/keyboard_translation.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/keyboard_protocol/keyboard_matrix_debounce.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_keyboard_boot/ef800_keyboard_boot.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_update/ef800_update.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_keyboard_update/ef800_keyboard_update_protocol.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_watchdog/ef800_watchdog.c
)

target_include_directories(app PRIVATE
	src
	${CMAKE_CURRENT_LIST_DIR}/../../shared/keyboard_protocol
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_keyboard_boot
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_update
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_keyboard_update
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_watchdog
)
