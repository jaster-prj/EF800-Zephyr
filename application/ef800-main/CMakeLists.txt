# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(PLUGIN_MANAGER_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../PluginManager" CACHE PATH
	"PluginManager source directory")
if(NOT EXISTS "${PLUGIN_MANAGER_DIR}/zephyr/module.yml")
	message(FATAL_ERROR "PLUGIN_MANAGER_DIR is not a PluginManager source tree")
endif()
list(APPEND ZEPHYR_EXTRA_MODULES "${PLUGIN_MANAGER_DIR}")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ef800_main)

target_sources(app PRIVATE
	src/main.c
	src/keyboard_host.c
	src/ef800_plugin_host.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/keyboard_protocol/keyboard_translation.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_main/ef800_time.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_main/ef800_main.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_runtime/ef800_runtime_state.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_drivers/ef800_mcp23017.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_drivers/ef800_max17043.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_drivers/ef800_t6963.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_persistence/ef800_persistence.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_persistence/ef800_persistence_legacy.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_persistence/ef800_persistence_zephyr.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_persistence/ef800_workspace.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_position/ef800_position.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_measurement/ef800_measurement.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_measurement/ef800_device_plugin.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_measurement/ef800_measurement_zephyr.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_ports.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_port_uart.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_debug.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_console.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_log_backend.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_shell_transport.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_mcumgr_transport.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_console_usb.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_watchdog/ef800_watchdog.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_update/ef800_update.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_update/ef800_metadata.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_main_usb_msc.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_storage/ef800_storage_owner.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_plugin/ef800_plugin_registry.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_plugin/ef800_calculation_service.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_plugin/ef800_plugin_screen.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_plugin/ef800_plugin_display.c
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_plugin/ef800_plugin_session.c
)

target_include_directories(app PRIVATE
	src
	${CMAKE_CURRENT_LIST_DIR}/../../shared/keyboard_protocol
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_main
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_runtime
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_drivers
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_persistence
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_position
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_measurement
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_update
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_storage
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_watchdog
	${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_plugin
	${CMAKE_CURRENT_LIST_DIR}/../plugins/measurement-plugin/include
)
