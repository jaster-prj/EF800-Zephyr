# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ef800_main)

target_sources(app PRIVATE
	src/main.c
	src/keyboard_host.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/keyboard_protocol/keyboard_translation.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_main/ef800_time.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_main/ef800_main.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_drivers/ef800_mcp23017.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_drivers/ef800_max17043.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_drivers/ef800_t6963.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_persistence/ef800_persistence.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_persistence/ef800_persistence_legacy.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_persistence/ef800_persistence_zephyr.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_persistence/ef800_workspace.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_position/ef800_position.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_measurement/ef800_measurement.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_measurement/ef800_measurement_zephyr.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_ports.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_port_uart.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_debug.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_console.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_log_backend.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_update/ef800_update.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_update/ef800_metadata.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_main_usb_msc.c
)

target_include_directories(app PRIVATE
	src
	${CMAKE_CURRENT_LIST_DIR}/../../shared/keyboard_protocol
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_main
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_drivers
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_persistence
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_position
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_measurement
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_update
)
