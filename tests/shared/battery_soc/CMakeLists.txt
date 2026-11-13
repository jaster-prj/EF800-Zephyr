# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.28.0)
set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_LIST_DIR}/boards/native_sim.overlay)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ef800_shared_battery_soc_test)

target_compile_definitions(app PRIVATE
	CONFIG_EF800_MAIN_RUNTIME_RTC_PERIOD_MS=1000
	CONFIG_EF800_MAIN_RUNTIME_BATTERY_PERIOD_MS=10000
)

target_sources(app PRIVATE
	src/main.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_battery/ef800_battery_soc.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_battery/ef800_battery_adc_ring.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_battery/ef800_battery_soc_gauge.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_battery/ef800_battery_soc_store.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_runtime/ef800_runtime_state.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_drivers/ef800_max17043.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/virtual_ef800/profile/virtual_ef800_profile.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/virtual_ef800/battery/virtual_ef800_battery.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_persistence/ef800_persistence.c
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_persistence/ef800_persistence_fake.c
)
target_include_directories(app PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_battery
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_drivers
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/virtual_ef800/profile
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/virtual_ef800/serial
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_runtime
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_main
	${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_persistence
)
