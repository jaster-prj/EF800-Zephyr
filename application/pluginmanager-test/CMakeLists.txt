# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

set(PLUGIN_MANAGER_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../modules/lib/PluginManager" CACHE PATH
    "PluginManager source directory")
if(BOARD STREQUAL "qemu_cortex_m3" OR BOARD STREQUAL "mps2_an385")
    add_compile_definitions(PM_MANAGER_MAX_DISCOVERED=4)
endif()
if(NOT EXISTS "${PLUGIN_MANAGER_DIR}/zephyr/module.yml")
    message(FATAL_ERROR "PLUGIN_MANAGER_DIR is not a PluginManager source tree")
endif()
list(APPEND ZEPHYR_EXTRA_MODULES "${PLUGIN_MANAGER_DIR}")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ef800_pluginmanager_test)

set(MEASUREMENT_PLUGIN_DIR "${CMAKE_CURRENT_LIST_DIR}/../plugins/measurement-plugin")
set(PLUGIN_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/measurement-plugins")
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(ELTA3_PACKAGE "${PLUGIN_BUILD_DIR}/elta3_plugin.pmp")
set(FTS3XX_PACKAGE "${PLUGIN_BUILD_DIR}/fts3xx_plugin.pmp")
set(ELTA3_PACKAGE_C "${GENERATED_DIR}/elta3_package.c")
set(FTS3XX_PACKAGE_C "${GENERATED_DIR}/fts3xx_package.c")

get_filename_component(ARM_COMPILER_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
get_filename_component(ARM_COMPILER_NAME "${CMAKE_C_COMPILER}" NAME)
string(REGEX REPLACE "-gcc$" "" ARM_COMPILER_STEM "${ARM_COMPILER_NAME}")
set(ARM_TOOLCHAIN_PREFIX "${ARM_COMPILER_DIR}/${ARM_COMPILER_STEM}")

add_custom_command(
    OUTPUT "${ELTA3_PACKAGE}" "${FTS3XX_PACKAGE}"
    COMMAND ${CMAKE_COMMAND} -S "${MEASUREMENT_PLUGIN_DIR}" -B "${PLUGIN_BUILD_DIR}"
        -DPM_ARM_TOOLCHAIN_PREFIX=${ARM_TOOLCHAIN_PREFIX}
        -DPLUGIN_MANAGER_DIR=${PLUGIN_MANAGER_DIR}
    COMMAND ${CMAKE_COMMAND} --build "${PLUGIN_BUILD_DIR}"
        --target elta3_plugin fts3xx_plugin
    DEPENDS
        "${MEASUREMENT_PLUGIN_DIR}/CMakeLists.txt"
        "${MEASUREMENT_PLUGIN_DIR}/src/elta3.c"
        "${MEASUREMENT_PLUGIN_DIR}/src/fts3xx.c"
        "${MEASUREMENT_PLUGIN_DIR}/src/protocol.c"
        "${MEASUREMENT_PLUGIN_DIR}/src/plugin_runtime.c"
        "${MEASUREMENT_PLUGIN_DIR}/src/descriptor.c"
        "${MEASUREMENT_PLUGIN_DIR}/tools/package_plugin.py"
        "${MEASUREMENT_PLUGIN_DIR}/tools/verify_package.py"
    VERBATIM)

add_custom_command(
    OUTPUT "${ELTA3_PACKAGE_C}"
    COMMAND ${Python3_EXECUTABLE}
        "${MEASUREMENT_PLUGIN_DIR}/tools/binary_to_c.py"
        "${ELTA3_PACKAGE}" "${ELTA3_PACKAGE_C}" --symbol elta3_package
    DEPENDS "${ELTA3_PACKAGE}"
        "${MEASUREMENT_PLUGIN_DIR}/tools/binary_to_c.py"
    VERBATIM)

add_custom_command(
    OUTPUT "${FTS3XX_PACKAGE_C}"
    COMMAND ${Python3_EXECUTABLE}
        "${MEASUREMENT_PLUGIN_DIR}/tools/binary_to_c.py"
        "${FTS3XX_PACKAGE}" "${FTS3XX_PACKAGE_C}" --symbol fts3xx_package
    DEPENDS "${FTS3XX_PACKAGE}"
        "${MEASUREMENT_PLUGIN_DIR}/tools/binary_to_c.py"
    VERBATIM)

target_include_directories(app BEFORE PRIVATE
    "${PLUGIN_MANAGER_DIR}/include"
    "${MEASUREMENT_PLUGIN_DIR}/include")
target_sources(app PRIVATE src/main.c src/host_policy.c src/calculation_plugin.c
    src/calculation_service.c
    "${ELTA3_PACKAGE_C}" "${FTS3XX_PACKAGE_C}")
