# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.28.0)

set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(virtual_ef800_keyboard_test)

target_sources(app PRIVATE
	src/main.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/virtual_ef800/keyboard/virtual_ef800_keyboard.c
	${CMAKE_CURRENT_LIST_DIR}/../../../application/ef800-keyboard/src/keyboard_key_names.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/keyboard_protocol/keyboard_translation.c
)

target_include_directories(app PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/virtual_ef800/keyboard
	${CMAKE_CURRENT_LIST_DIR}/../../../application/ef800-keyboard/src
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/keyboard_protocol
)
