# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(keyboard_protocol_test)

target_sources(app PRIVATE
	src/main.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/keyboard_protocol/keyboard_translation.c
	${CMAKE_CURRENT_LIST_DIR}/../../application/ef800-main/src/keyboard_host.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_main/ef800_time.c
)

target_include_directories(app PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/../../shared/keyboard_protocol
	${CMAKE_CURRENT_LIST_DIR}/../../application/ef800-main/src
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_main
)
