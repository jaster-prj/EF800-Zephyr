# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_LIST_DIR}/boards/native_sim.overlay)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ef800_shared_metadata_test)
target_compile_definitions(app PRIVATE EF800_TEST_METADATA)
target_sources(app PRIVATE
    src/main.c
    ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_update/ef800_update.c
    ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_update/ef800_metadata.c
    ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_storage/ef800_qspi_layout.c
    ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_boot/ef800_boot_decision.c)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_update
    ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_boot)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_storage)
