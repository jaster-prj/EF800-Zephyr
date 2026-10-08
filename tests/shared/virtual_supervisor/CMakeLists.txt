# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ef800_virtual_supervisor_test)

target_sources(app PRIVATE
	src/main.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_ui/ef800_ui.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_virtual_plugin/ef800_virtual_plugin.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_virtual_supervisor/ef800_virtual_supervisor.c
)

target_include_directories(app PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_ui
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_virtual_plugin
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_virtual_supervisor
)
