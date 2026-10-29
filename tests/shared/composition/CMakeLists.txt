# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ef800_shared_composition_test)
target_sources(app PRIVATE
    src/main.c
    ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_composition/ef800_composition.c
    ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_persistence/ef800_persistence.c
    ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_persistence/ef800_persistence_fake.c)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_composition
    ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_persistence)
