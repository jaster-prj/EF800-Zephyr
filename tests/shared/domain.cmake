target_compile_options(app PRIVATE -Wno-unused-function)
target_sources(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/regression/src/main.c
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/keyboard_protocol/keyboard_translation.c
    ${CMAKE_CURRENT_LIST_DIR}/../../application/ef800-keyboard/src/keyboard_key_names.c
    ${CMAKE_CURRENT_LIST_DIR}/../../application/ef800-main/src/keyboard_host.c
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_main/ef800_time.c
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_persistence/ef800_persistence.c
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_persistence/ef800_persistence_legacy.c
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_persistence/ef800_persistence_zephyr.c
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_persistence/ef800_workspace.c
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_persistence/ef800_persistence_fake.c
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_position/ef800_position.c
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_measurement/ef800_measurement.c
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_measurement/ef800_device_plugin.c
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_ports.c
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_port_uart.c
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports/ef800_debug.c
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_update/ef800_update.c)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/keyboard_protocol
    ${CMAKE_CURRENT_LIST_DIR}/../../application/ef800-main/src
    ${CMAKE_CURRENT_LIST_DIR}/../../application/ef800-keyboard/src
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_main
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_persistence
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_position
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_measurement
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_ports
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/ef800_update)
