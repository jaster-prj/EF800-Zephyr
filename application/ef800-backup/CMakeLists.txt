# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(DTS_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ef800_backup)

target_sources(app PRIVATE
	src/main.c
	src/ef800_backup_composition.c
	src/ef800_backup_usb_mtp.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_composition/ef800_composition.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_persistence/ef800_persistence.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_watchdog/ef800_watchdog.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_storage/ef800_qspi_layout.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_lcd_contrast/ef800_lcd_contrast.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_lcd_contrast/ef800_lcd_contrast_storage.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_lcd_contrast/ef800_lcd_contrast_stm32.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_console.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_log_backend.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_update/ef800_update.c
)

target_include_directories(app PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_watchdog
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_update
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_measurement
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_storage
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_lcd_contrast
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_composition
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_persistence
	${CMAKE_CURRENT_LIST_DIR}/src
)

target_include_directories(app PRIVATE
)
