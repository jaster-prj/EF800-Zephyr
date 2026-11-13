# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.28.0)

set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../boards/ef800_keyboard)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(keyboard_updater)

target_sources(app PRIVATE
	src/main.c
	src/keyboard_updater_flash.c
	src/keyboard_updater_session.c
	src/keyboard_updater_transport.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_keyboard_update/ef800_keyboard_update_protocol.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_update/ef800_update.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_watchdog/ef800_watchdog.c
)

target_include_directories(app PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_keyboard_update
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_update
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_watchdog
)
