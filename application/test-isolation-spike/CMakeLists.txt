# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(KCONFIG_ROOT ${CMAKE_CURRENT_LIST_DIR}/Kconfig)

if(BOARD STREQUAL "nucleo_f767zi")
	set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_LIST_DIR}/boards/nucleo_f767zi.overlay)
elseif(BOARD STREQUAL "nucleo_wb55rg")
	set(DTC_OVERLAY_FILE
		${CMAKE_CURRENT_LIST_DIR}/app.overlay
		${CMAKE_CURRENT_LIST_DIR}/boards/nucleo_wb55rg.overlay)
elseif(BOARD STREQUAL "ef800")
	set(DTC_OVERLAY_FILE
		${CMAKE_CURRENT_LIST_DIR}/../test-hwdebug/app.overlay
		${CMAKE_CURRENT_LIST_DIR}/boards/ef800.overlay)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test-isolation-spike)

target_sources(app PRIVATE src/main.c src/usb_cdc_host.c)

if(CONFIG_TEST_ISOLATION_SPIKE_USB_CDC_TEST)
	set(PM_PLUGIN_MANAGER_ROOT "${ZEPHYR_BASE}/../modules/lib/PluginManager")
	set(PM_DUMMY_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/dummy-plugin")
	set(PM_DUMMY_PLUGIN_OBJECT "${PM_DUMMY_BUILD_DIR}/dummy_plugin.o")
	set(PM_DUMMY_PLUGIN_ELF "${PM_DUMMY_BUILD_DIR}/dummy_plugin.elf")
	set(PM_DUMMY_PACKAGE_TOOL "${PM_DUMMY_BUILD_DIR}/plugin_manager_package_tool")
	set(PM_DUMMY_PACKAGE "${PM_DUMMY_BUILD_DIR}/dummy_plugin.pmp")
	set(PM_DUMMY_PACKAGE_SOURCE "${PM_DUMMY_BUILD_DIR}/dummy_plugin_package.c")
	set(PM_DUMMY_LINKER_SCRIPT "${PM_DUMMY_BUILD_DIR}/dummy_plugin_memory.ld")
	find_program(PM_HOST_C_COMPILER NAMES cc gcc REQUIRED)
	target_sources(app PRIVATE src/pm_dummy_plugin_host.c)

	configure_file(
		${CMAKE_CURRENT_LIST_DIR}/dummy_plugin/memory.ld.in
		${PM_DUMMY_LINKER_SCRIPT} @ONLY)

	add_custom_command(
		OUTPUT ${PM_DUMMY_PLUGIN_ELF}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${PM_DUMMY_BUILD_DIR}
		COMMAND ${CMAKE_C_COMPILER}
			-mcpu=cortex-m4 -mthumb -mfloat-abi=soft -ffreestanding
			-fno-builtin -fno-common -ffunction-sections -fdata-sections
			-I${PM_PLUGIN_MANAGER_ROOT}/include
			-I${CMAKE_CURRENT_LIST_DIR}/dummy_plugin
			-c ${CMAKE_CURRENT_LIST_DIR}/dummy_plugin/plugin.c
			-o ${PM_DUMMY_PLUGIN_OBJECT}
		COMMAND ${CMAKE_C_COMPILER}
			-mcpu=cortex-m4 -mthumb -mfloat-abi=soft
			-r -nostdlib -nostartfiles -nodefaultlibs
			-T${PM_DUMMY_LINKER_SCRIPT} -Wl,--emit-relocs
			-o ${PM_DUMMY_PLUGIN_ELF} ${PM_DUMMY_PLUGIN_OBJECT}
		DEPENDS
			${CMAKE_CURRENT_LIST_DIR}/dummy_plugin/plugin.c
			${CMAKE_CURRENT_LIST_DIR}/dummy_plugin/dummy_plugin_api.h
			${PM_DUMMY_LINKER_SCRIPT}
		VERBATIM)

	add_custom_command(
		OUTPUT ${PM_DUMMY_PACKAGE_TOOL}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${PM_DUMMY_BUILD_DIR}
		COMMAND ${PM_HOST_C_COMPILER} -std=c11
			-I${PM_PLUGIN_MANAGER_ROOT}/include
			${PM_PLUGIN_MANAGER_ROOT}/tests/package_tool.c
			${PM_PLUGIN_MANAGER_ROOT}/src/plugin_package.c
			-o ${PM_DUMMY_PACKAGE_TOOL}
		DEPENDS
			${PM_PLUGIN_MANAGER_ROOT}/tests/package_tool.c
			${PM_PLUGIN_MANAGER_ROOT}/src/plugin_package.c
		VERBATIM)

	add_custom_command(
		OUTPUT ${PM_DUMMY_PACKAGE}
		COMMAND ${PM_DUMMY_PACKAGE_TOOL} ${PM_DUMMY_PLUGIN_ELF} ${PM_DUMMY_PACKAGE}
		DEPENDS ${PM_DUMMY_PLUGIN_ELF} ${PM_DUMMY_PACKAGE_TOOL}
		VERBATIM)

	add_custom_command(
		OUTPUT ${PM_DUMMY_PACKAGE_SOURCE}
		COMMAND ${Python3_EXECUTABLE}
			${CMAKE_CURRENT_LIST_DIR}/dummy_plugin/embed_package.py
			${PM_DUMMY_PACKAGE} ${PM_DUMMY_PACKAGE_SOURCE}
		DEPENDS
			${PM_DUMMY_PACKAGE}
			${CMAKE_CURRENT_LIST_DIR}/dummy_plugin/embed_package.py
		VERBATIM)
	target_sources(app PRIVATE ${PM_DUMMY_PACKAGE_SOURCE})
endif()

if(CONFIG_TEST_ISOLATION_SPIKE_EXTERNAL_SRAM)
	zephyr_linker_sources(RAM_SECTIONS
		${CMAKE_CURRENT_LIST_DIR}/plugin-sections-external.ld)
else()
	zephyr_linker_sources(RAM_SECTIONS ${CMAKE_CURRENT_LIST_DIR}/plugin-sections.ld)
endif()
