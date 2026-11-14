# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.28.0)

set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ef800_shared_workspace_editor_test)

target_sources(app PRIVATE
	src/main.c
	${CMAKE_CURRENT_LIST_DIR}/../../../application/ef800-application/src/ef800_workspace_browser.c
	${CMAKE_CURRENT_LIST_DIR}/../../../application/ef800-application/src/ef800_workspace_row_editor.c
	${CMAKE_CURRENT_LIST_DIR}/../../../application/ef800-application/src/ef800_position_editor.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_position/ef800_position.c
)

target_include_directories(app PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/../../../application/ef800-application/src
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_persistence
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_position
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_measurement
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_update
)
