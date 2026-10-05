# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ef800_shared_plugin_test)

target_sources(app PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/src/main.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_plugin_registry.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_calculation_service.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_plugin_screen.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_plugin_display.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_plugin_session.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_keyboard_events.c
	${CMAKE_CURRENT_LIST_DIR}/../../../application/ef800-main/src/keyboard_host.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/keyboard_protocol/keyboard_translation.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_measurement/ef800_device_plugin.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_measurement/ef800_measurement.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_persistence/ef800_workspace.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_persistence/ef800_persistence.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_persistence/ef800_persistence_fake.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_update/ef800_update.c
)

target_include_directories(app PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_measurement
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_update
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_persistence
	${CMAKE_CURRENT_LIST_DIR}/../../../application/ef800-main/src
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/keyboard_protocol
)
