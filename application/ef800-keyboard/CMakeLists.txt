# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../boards/ef800_keyboard)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ef800_keyboard)

target_sources(app PRIVATE
	src/main.c
	src/keyboard_key_names.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/keyboard_protocol/keyboard_translation.c
)

target_include_directories(app PRIVATE
	src
	${CMAKE_CURRENT_LIST_DIR}/../../shared/keyboard_protocol
)
