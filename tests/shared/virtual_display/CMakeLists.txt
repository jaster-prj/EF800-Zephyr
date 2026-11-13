# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.28.0)

set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(virtual_ef800_display_test)

set(CGROM_SOURCE ${CMAKE_CURRENT_LIST_DIR}/../../../docs/CG_RAM0101.txt)
set(CGROM_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/cgrom_0101.h)
add_custom_command(
	OUTPUT ${CGROM_HEADER}
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/../../../application/tlx-711a/tools/generate_cgrom.py
		${CGROM_SOURCE} ${CGROM_HEADER}
	DEPENDS ${CGROM_SOURCE} ${CMAKE_CURRENT_LIST_DIR}/../../../application/tlx-711a/tools/generate_cgrom.py
)
add_custom_target(virtual_ef800_display_cgrom DEPENDS ${CGROM_HEADER})
add_dependencies(app virtual_ef800_display_cgrom)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

target_sources(app PRIVATE
	src/main.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/virtual_ef800/display/virtual_ef800_display.c
	${CMAKE_CURRENT_LIST_DIR}/../../../application/tlx-711a/src/t6963_emulator.c
)

target_include_directories(app PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/virtual_ef800/display
	${CMAKE_CURRENT_LIST_DIR}/../../../application/tlx-711a/src
)
