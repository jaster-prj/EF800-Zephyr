# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tlx_711a)

set(CGROM_SOURCE ${CMAKE_CURRENT_LIST_DIR}/../../docs/CG_RAM0101.txt)
set(CGROM_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/cgrom_0101.h)
add_custom_command(
	OUTPUT ${CGROM_HEADER}
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/generate_cgrom.py
		${CGROM_SOURCE} ${CGROM_HEADER}
	DEPENDS ${CGROM_SOURCE} ${CMAKE_CURRENT_LIST_DIR}/tools/generate_cgrom.py
)
add_custom_target(tlx711a_cgrom DEPENDS ${CGROM_HEADER})

target_sources(app PRIVATE
	src/main.c
	src/lcd_bus.c
	src/t6963_emulator.c
	src/transport.c
)

add_dependencies(app tlx711a_cgrom)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

target_include_directories(app PRIVATE src)
