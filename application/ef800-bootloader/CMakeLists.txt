# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ef800_bootloader)

target_sources(app PRIVATE
	src/main.c
	src/boot_update.c
	src/boot_watchdog_early.c
	src/boot_watchdog_reset.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_boot/ef800_boot_decision.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_update/ef800_update.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_update/ef800_metadata.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_console.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_watchdog/ef800_watchdog.c
)


target_include_directories(app PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_update
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_measurement
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_boot
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_watchdog
)
