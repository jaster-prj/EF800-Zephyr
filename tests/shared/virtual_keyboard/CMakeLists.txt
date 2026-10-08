# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ef800_virtual_keyboard_test)

target_sources(app PRIVATE
	src/main.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_virtual_keyboard/ef800_virtual_keyboard.c
	${CMAKE_CURRENT_LIST_DIR}/../../../application/ef800-keyboard/src/keyboard_key_names.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/keyboard_protocol/keyboard_translation.c
)

target_include_directories(app PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_virtual_keyboard
	${CMAKE_CURRENT_LIST_DIR}/../../../application/ef800-keyboard/src
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/keyboard_protocol
)
