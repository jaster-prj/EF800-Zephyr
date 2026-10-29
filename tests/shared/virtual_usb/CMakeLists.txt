# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_LIST_DIR}/boards/native_sim.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ef800_virtual_usb_peer_test)
target_compile_definitions(app PRIVATE
	CONFIG_EF800_USB_VID=0x2fe3
	CONFIG_EF800_USB_PID=0xef87)

target_sources(app PRIVATE
	src/main.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_virtual_usb/ef800_virtual_usb_peer.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_ports/ef800_main_usb_mtp.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_ports/ef800_console_usb.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_ports/ef800_console.c)

target_include_directories(app PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_virtual_usb
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_ports
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_measurement
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_update
	${ZEPHYR_BASE}/subsys/usb/host)
