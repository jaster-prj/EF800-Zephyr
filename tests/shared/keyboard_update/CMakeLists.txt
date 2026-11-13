# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.28.0)
set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ef800_keyboard_update_test)

target_sources(app PRIVATE
	src/main.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_update/ef800_keyboard_update.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_update/ef800_update.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_keyboard_update/ef800_keyboard_update_protocol.c
	${CMAKE_CURRENT_LIST_DIR}/../../../application/ef800-main/src/ef800_keyboard_update_service.c)

target_include_directories(app PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_update
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_keyboard_update
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_keyboard_boot
	${CMAKE_CURRENT_LIST_DIR}/../../../application/ef800-main/src)
