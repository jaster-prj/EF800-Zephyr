cmake_minimum_required(VERSION 3.20)
project(ef800_measurement_plugins C)

enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(PLUGIN_MANAGER_DIR "" CACHE PATH "Optional PluginManager source tree")
set(PM_ARM_TOOLCHAIN_PREFIX "" CACHE STRING "ARM compiler prefix, without -gcc")
option(PM_BUILD_NATIVE_SIM "Build native_sim plugin packages" OFF)
set(EF800_PLUGIN_VERSION "1.0.0")

set(COMMON_SOURCES common/protocol.c common/plugin_runtime.c)
set(COMMON_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/compat ${CMAKE_CURRENT_LIST_DIR}/common)

function(add_host_plugin_test target source)
    add_executable(${target} tests/test_plugin.c ${source} ${COMMON_SOURCES})
    target_include_directories(${target} PRIVATE ${COMMON_INCLUDES})
    target_compile_features(${target} PRIVATE c_std_11)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    endif()
endfunction()

add_host_plugin_test(elta3_host_test elta3/src/elta3.c)
target_compile_definitions(elta3_host_test PRIVATE
    TEST_ELTA3=1
    TEST_BAUD=1200u TEST_DATA_BITS=7u TEST_PARITY=1u TEST_STOP_BITS=2u
    TEST_CONTROL=48u TEST_HAS_INPUT=1)
add_test(NAME elta3_protocol COMMAND elta3_host_test)

add_host_plugin_test(fennel1_host_test fennel1/src/fennel1.c)
target_compile_definitions(fennel1_host_test PRIVATE
    TEST_ELTA3=0
    TEST_BAUD=9600u TEST_DATA_BITS=8u TEST_PARITY=0u TEST_STOP_BITS=1u
    TEST_CONTROL=0u TEST_HAS_INPUT=0)
add_test(NAME fennel1_protocol COMMAND fennel1_host_test)

if(PM_ARM_TOOLCHAIN_PREFIX)
    set(ARM_CC "${PM_ARM_TOOLCHAIN_PREFIX}-gcc")
    set(PM_PLUGIN_TEXT_ORIGIN 0x00000000)
    set(PM_PLUGIN_TEXT_LENGTH 0x00002000)
    set(PM_PLUGIN_RODATA_ORIGIN 0x00002000)
    set(PM_PLUGIN_RODATA_LENGTH 0x00002000)
    set(PM_PLUGIN_DATA_ORIGIN 0x20010000)
    set(PM_PLUGIN_DATA_LENGTH 0x00000800)
    if(PLUGIN_MANAGER_DIR AND EXISTS "${PLUGIN_MANAGER_DIR}/cmake/plugin_manager_plugin.ld")
        set(PM_PLUGIN_LINKER_SCRIPT "${PLUGIN_MANAGER_DIR}/cmake/plugin_manager_plugin.ld")
    else()
        set(PM_PLUGIN_LINKER_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/cmake/plugin_sections.ld")
    endif()
    configure_file(cmake/plugin_memory.ld.in ${CMAKE_CURRENT_BINARY_DIR}/plugin_memory.ld @ONLY)

    function(add_arm_plugin target package_dir source plugin_id plugin_name)
        set(image ${CMAKE_CURRENT_BINARY_DIR}/${target}.elf)
        set(package ${CMAKE_CURRENT_BINARY_DIR}/${target}.pmp)
        add_custom_command(OUTPUT ${image}
            COMMAND ${ARM_CC} -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft -mlong-calls
                -std=c11 -Os -ffreestanding -fno-builtin -fno-common
                -ffunction-sections -fdata-sections -Wall -Wextra -Werror
                -I${CMAKE_CURRENT_LIST_DIR}/include
                -I${CMAKE_CURRENT_LIST_DIR}/compat
                 -I${CMAKE_CURRENT_LIST_DIR}/common
                -DEF800_PLUGIN_ID=${plugin_id}u
                 ${CMAKE_CURRENT_LIST_DIR}/${source}
                 ${CMAKE_CURRENT_LIST_DIR}/common/protocol.c
                 ${CMAKE_CURRENT_LIST_DIR}/common/plugin_runtime.c
                 ${CMAKE_CURRENT_LIST_DIR}/${package_dir}/descriptor.c
                -r -nostdlib -nostartfiles -nodefaultlibs
                -T${CMAKE_CURRENT_BINARY_DIR}/plugin_memory.ld
                -o ${image}
                 DEPENDS ${source} ${COMMON_SOURCES} ${package_dir}/descriptor.c
                ${CMAKE_CURRENT_BINARY_DIR}/plugin_memory.ld)
        add_custom_command(OUTPUT ${package}
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/package_plugin.py
                --image ${image} --output ${package} --plugin-id ${plugin_id}
                --name "${plugin_name}" --version ${EF800_PLUGIN_VERSION}
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/verify_package.py
                ${package} --plugin-id ${plugin_id}
            DEPENDS ${image} tools/package_plugin.py tools/verify_package.py)
        add_custom_target(${target} ALL DEPENDS ${package})
    endfunction()

    add_arm_plugin(elta3_plugin elta3 elta3/src/elta3.c 2 "EF800 ELTA3")
    add_arm_plugin(fennel1_plugin fennel1 fennel1/src/fennel1.c 3 "EF800 Fennel1")
endif()

if(PM_BUILD_NATIVE_SIM)
    set(NATIVE_CC "${CMAKE_C_COMPILER}")
    set(NATIVE_LINKER_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/cmake/native_sections.ld")
    # Keep one text and one rodata section for the bounded native PluginManager
    # ELF loader.  The ARM linker path retains section granularity and MPU
    # placement; native-sim packages are relocatable host artifacts.
    set(NATIVE_CFLAGS -m32 -std=c11 -Os -ffreestanding -fno-builtin -fno-common
        -fno-pic -fno-pie -Wall -Wextra -Werror)
    function(add_native_plugin target package_dir source plugin_id plugin_name)
        set(image ${CMAKE_CURRENT_BINARY_DIR}/${target}.elf)
        set(package ${CMAKE_CURRENT_BINARY_DIR}/${target}.pmp)
        add_custom_command(OUTPUT ${image}
            COMMAND ${NATIVE_CC} ${NATIVE_CFLAGS}
                -I${CMAKE_CURRENT_LIST_DIR}/include
                -I${CMAKE_CURRENT_LIST_DIR}/compat
                -I${CMAKE_CURRENT_LIST_DIR}/common
                -DEF800_PLUGIN_ID=${plugin_id}u
                ${CMAKE_CURRENT_LIST_DIR}/${source}
                ${CMAKE_CURRENT_LIST_DIR}/common/protocol.c
                ${CMAKE_CURRENT_LIST_DIR}/common/plugin_runtime.c
                ${CMAKE_CURRENT_LIST_DIR}/common/freestanding.c
                ${CMAKE_CURRENT_LIST_DIR}/${package_dir}/descriptor.c
                 -r -nostdlib -nostartfiles -nodefaultlibs
                 -Wl,-T,${NATIVE_LINKER_SCRIPT} -o ${image}
            DEPENDS ${source} ${COMMON_SOURCES} ${package_dir}/descriptor.c)
        add_custom_command(OUTPUT ${package}
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/package_plugin.py
                --image ${image} --output ${package} --plugin-id ${plugin_id}
                --name "${plugin_name}" --version ${EF800_PLUGIN_VERSION} --target 2
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/verify_package.py
                ${package} --plugin-id ${plugin_id}
            DEPENDS ${image} tools/package_plugin.py tools/verify_package.py)
        add_custom_target(${target} ALL DEPENDS ${package})
    endfunction()
    add_native_plugin(elta3_native_plugin elta3 elta3/src/elta3.c 2 "EF800 ELTA3 native_sim")
    add_native_plugin(fennel1_native_plugin fennel1 fennel1/src/fennel1.c 3 "EF800 Fennel1 native_sim")
endif()
