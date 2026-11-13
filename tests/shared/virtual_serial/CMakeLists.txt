# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(virtual_ef800_serial_test)

target_sources(app PRIVATE
	src/main.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/virtual_ef800/serial/virtual_ef800_serial.c
)

target_include_directories(app PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/virtual_ef800/serial
)
