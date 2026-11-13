# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(DTS_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(PLUGIN_MANAGER_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../modules/lib/PluginManager" CACHE PATH
	"PluginManager source directory")
if(NOT EXISTS "${PLUGIN_MANAGER_DIR}/zephyr/module.yml")
	message(FATAL_ERROR "PLUGIN_MANAGER_DIR is not a PluginManager source tree")
endif()
list(APPEND ZEPHYR_EXTRA_MODULES "${PLUGIN_MANAGER_DIR}")
set(KCONFIG_ROOT ${CMAKE_CURRENT_LIST_DIR}/Kconfig)

if(BOARD STREQUAL "native_sim")
	set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_LIST_DIR}/native_sim.overlay)
else()
	set(DTC_OVERLAY_FILE
		${CMAKE_CURRENT_LIST_DIR}/app.overlay
		${CMAKE_CURRENT_LIST_DIR}/boards/ef800-sram.overlay)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ef800_main)

if(CONFIG_EF800_MAIN_PLUGIN_RECOVERY_TEST_DIRECTORY)
	# The recovery-test volume contains exactly the fault and healthy fixtures.
	# Keep PluginManager's otherwise-large default discovery table bounded in
	# this dedicated image so it fits the L496 malloc arena.
	zephyr_compile_definitions(PM_MANAGER_MAX_DISCOVERED=9)
	elseif(BOARD STREQUAL "ef800")
	# Keep PluginManager's fixed discovery table bounded while supporting the
	# two serial instruments and the port-free dummy calculation package.
	zephyr_compile_definitions(PM_MANAGER_MAX_DISCOVERED=3)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/ef800-main-sources.cmake)
list(LENGTH EF800_MAIN_COMMON_SOURCES EF800_MAIN_COMMON_SOURCE_COUNT)
message(STATUS "EF800 common production source manifest: ${EF800_MAIN_COMMON_SOURCE_COUNT} sources")
target_sources(app PRIVATE ${EF800_MAIN_COMMON_SOURCES})
target_include_directories(app PRIVATE ${EF800_MAIN_COMMON_INCLUDE_DIRS})

if(BOARD STREQUAL "native_sim")
	target_compile_definitions(app PRIVATE PM_MANAGER_MAX_DISCOVERED=16)
	set(CGROM_SOURCE ${CMAKE_CURRENT_LIST_DIR}/../../docs/CG_RAM0101.txt)
	set(CGROM_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/cgrom_0101.h)
	add_custom_command(
		OUTPUT ${CGROM_HEADER}
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/../tlx-711a/tools/generate_cgrom.py
			${CGROM_SOURCE} ${CGROM_HEADER}
		DEPENDS ${CGROM_SOURCE} ${CMAKE_CURRENT_LIST_DIR}/../tlx-711a/tools/generate_cgrom.py
	)
	add_custom_target(ef800_main_cgrom DEPENDS ${CGROM_HEADER})
	add_dependencies(app ef800_main_cgrom)
	target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

		target_sources(app PRIVATE
		${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_console_usb.c
		${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_main_usb_msc_native.c
		${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_main_usb_mtp.c
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/usb/virtual_ef800_usb_peer.c
		${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_watchdog/ef800_watchdog_native.c
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/keyboard/virtual_ef800_keyboard_controller.c
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/gpio/virtual_ef800_mcp23017.c
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/rtc/virtual_ef800_ds3231.c
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/gpio/virtual_ef800_gpio_emul.c
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/display/virtual_ef800_t6963_bus.c
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/gpio/virtual_ef800_wiring.c
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/frontpanel/virtual_ef800_frontpanel_bridge.c
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/serial/virtual_ef800_serial_peers.c
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/profile/virtual_ef800_profile.c
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/battery/virtual_ef800_battery.c
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/display/virtual_ef800_display.c
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/frontpanel/virtual_ef800_frontpanel.c
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/log/virtual_ef800_log.c
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/protocol/virtual_ef800_protocol.c
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/keyboard/virtual_ef800_keyboard.c
		${CMAKE_CURRENT_LIST_DIR}/../../application/ef800-keyboard/src/keyboard_key_names.c
		${CMAKE_CURRENT_LIST_DIR}/../../application/tlx-711a/src/t6963_emulator.c
	)
	target_include_directories(app PRIVATE
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/keyboard
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/display
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/frontpanel
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/log
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/protocol
		${CMAKE_CURRENT_LIST_DIR}/../../application/ef800-keyboard/src
		${CMAKE_CURRENT_LIST_DIR}/../../application/tlx-711a/src
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/usb
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/battery
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/core
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/gpio
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/profile
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/rtc
		${CMAKE_CURRENT_LIST_DIR}/../../shared/virtual_ef800/serial
		${ZEPHYR_BASE}/subsys/usb/host
	)
else()
	target_sources(app PRIVATE
		${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_lcd_contrast/ef800_lcd_contrast_stm32.c
		${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_console_usb.c
		${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_main_usb_msc.c
		${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_main_usb_mtp.c
		${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_watchdog/ef800_watchdog.c
	)
endif()
