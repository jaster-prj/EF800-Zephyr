# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.28.0)

set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_LIST_DIR}/boards/native_sim.overlay)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ef800_boot_update_test)

target_sources(app PRIVATE
	src/main.c
	${CMAKE_CURRENT_LIST_DIR}/../../../application/ef800-bootloader/src/boot_update.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_update/ef800_update.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_watchdog/ef800_watchdog_native.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_storage/ef800_qspi_layout.c)

target_include_directories(app PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/../../../application/ef800-bootloader/src
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_update
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_watchdog
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_storage)
