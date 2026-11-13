cmake_minimum_required(VERSION 3.20)
project(ef800_measurement_plugins C)

enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(PLUGIN_MANAGER_DIR "" CACHE PATH "Optional PluginManager source tree")
set(PM_ARM_TOOLCHAIN_PREFIX "" CACHE STRING "ARM compiler prefix, without -gcc")
option(PM_BUILD_NATIVE_SIM "Build native_sim plugin packages" OFF)
option(PM_BUILD_TARGET_RECOVERY_TESTS
    "Build test-only ARM calculation packages for EF800 recovery acceptance" OFF)
set(EF800_RECOVERY_TEST_FAULT_MODE 1 CACHE STRING
    "Test mode for recovery_fault_plugin (1..10)")
set(EF800_PLUGIN_VERSION "1.0.0")

set(COMMON_SOURCES common/protocol.c common/plugin_runtime.c)
set(COMMON_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/compat ${CMAKE_CURRENT_LIST_DIR}/common
    ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin)

function(add_host_plugin_test target source)
    add_executable(${target} tests/test_plugin.c ${source} ${COMMON_SOURCES}
        ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_plugin_worker_sdk.c
        ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_plugin_worker_abi.c)
    target_include_directories(${target} PRIVATE ${COMMON_INCLUDES})
    target_compile_features(${target} PRIVATE c_std_11)
    target_compile_definitions(${target} PRIVATE
        EF800_PLUGIN_EXECUTION_MODEL=EF800_PLUGIN_EXECUTION_USER_WORKER)
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

add_host_plugin_test(fts3xx_host_test fts3xx/src/fts3xx.c)
target_compile_definitions(fts3xx_host_test PRIVATE
    TEST_ELTA3=0
    TEST_BAUD=9600u TEST_DATA_BITS=8u TEST_PARITY=0u TEST_STOP_BITS=1u
    TEST_CONTROL=0u TEST_HAS_INPUT=0)
add_test(NAME fts3xx_protocol COMMAND fts3xx_host_test)

add_executable(dummy_calculation_test
    tests/test_dummy_calculation.c calculation-dummy/random_position.c)
target_include_directories(dummy_calculation_test PRIVATE ${COMMON_INCLUDES})
target_compile_features(dummy_calculation_test PRIVATE c_std_11)
if(MSVC)
    target_compile_options(dummy_calculation_test PRIVATE /W4)
else()
    target_compile_options(dummy_calculation_test PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME dummy_calculation_workspace_position COMMAND dummy_calculation_test)

add_executable(worker_sdk_test
    tests/test_worker_sdk.c
    ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_plugin_worker_sdk.c
    ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_plugin_worker_abi.c)
target_include_directories(worker_sdk_test PRIVATE ${COMMON_INCLUDES})
target_compile_features(worker_sdk_test PRIVATE c_std_11)
if(MSVC)
    target_compile_options(worker_sdk_test PRIVATE /W4)
else()
    target_compile_options(worker_sdk_test PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME worker_sdk_bounded_gateway COMMAND worker_sdk_test)

add_test(NAME plugin_memory_declaration_validation
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_LIST_DIR}/tools/test_verify_package.py)

if(PM_ARM_TOOLCHAIN_PREFIX)
    set(ARM_CC "${PM_ARM_TOOLCHAIN_PREFIX}-gcc")
    set(PM_PLUGIN_TEXT_ORIGIN 0x00000000)
    set(PM_PLUGIN_TEXT_LENGTH 0x00002000)
    set(PM_PLUGIN_RODATA_ORIGIN 0x00002000)
    set(PM_PLUGIN_RODATA_LENGTH 0x00002000)
    set(PM_PLUGIN_DATA_ORIGIN 0x20010000)
    set(PM_PLUGIN_DATA_LENGTH 0x00000800)
    # MPU alignment is an EF800 package contract, so use the application-owned
    # linker layout rather than a generic PluginManager default script.
    set(PM_PLUGIN_LINKER_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/cmake/plugin_sections.ld")
    configure_file(cmake/plugin_memory.ld.in ${CMAKE_CURRENT_BINARY_DIR}/plugin_memory.ld @ONLY)

    function(add_arm_plugin target package_dir source plugin_id plugin_name)
        set(image ${CMAKE_CURRENT_BINARY_DIR}/${target}.elf)
        set(package ${CMAKE_CURRENT_BINARY_DIR}/${target}.pmp)
        add_custom_command(OUTPUT ${image}
            COMMAND ${ARM_CC} -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -mlong-calls
                -std=c11 -Os -ffreestanding -fno-builtin -fno-common
                -ffunction-sections -fdata-sections -fno-jump-tables
                -Wall -Wextra -Werror
                -I${CMAKE_CURRENT_LIST_DIR}/include
                -I${CMAKE_CURRENT_LIST_DIR}/compat
                -I${CMAKE_CURRENT_LIST_DIR}/common
                -I${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin
                -DEF800_PLUGIN_ID=${plugin_id}u
                -DEF800_PLUGIN_TARGET_ARCH=1u
                -DEF800_PLUGIN_EXECUTION_MODEL=1u
                ${CMAKE_CURRENT_LIST_DIR}/${source}
                ${CMAKE_CURRENT_LIST_DIR}/common/protocol.c
                ${CMAKE_CURRENT_LIST_DIR}/common/plugin_runtime.c
                ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_plugin_worker_sdk.c
                ${CMAKE_CURRENT_LIST_DIR}/common/freestanding.c
                  ${CMAKE_CURRENT_LIST_DIR}/${package_dir}/descriptor.c
                 -r -nostdlib -nostartfiles -nodefaultlibs
                 -T${CMAKE_CURRENT_BINARY_DIR}/plugin_memory.ld
                -o ${image}
                DEPENDS ${source} ${COMMON_SOURCES}
                 ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_plugin_worker_sdk.c
                 ${CMAKE_CURRENT_LIST_DIR}/common/freestanding.c
                ${CMAKE_CURRENT_LIST_DIR}/include/ef800_plugin_ids.h
                ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_plugin_metadata.h
                 ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_plugin_worker_abi.h
                 ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_plugin_worker_sdk.h
                ${package_dir}/descriptor.c
                ${CMAKE_CURRENT_BINARY_DIR}/plugin_memory.ld)
        add_custom_command(OUTPUT ${package}
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/package_plugin.py
                 --image ${image} --output ${package} --plugin-id ${plugin_id}
                 --name "${plugin_name}" --version ${EF800_PLUGIN_VERSION}
                  --execution-model user-worker
                 --ram-size 2048 --stack-size 2048
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/verify_package.py
                ${package} --plugin-id ${plugin_id}
            DEPENDS ${image} tools/package_plugin.py tools/verify_package.py)
        add_custom_target(${target} ALL DEPENDS ${package})
    endfunction()

    add_arm_plugin(elta3_plugin elta3 elta3/src/elta3.c 2 "EF800 ELTA3")
    add_arm_plugin(fts3xx_plugin fts3xx fts3xx/src/fts3xx.c 3 "EF800 FTS3XX")

    set(DUMMY_CALC_IMAGE ${CMAKE_CURRENT_BINARY_DIR}/dummy_random_position.elf)
    set(DUMMY_CALC_PACKAGE ${CMAKE_CURRENT_BINARY_DIR}/dummy_random_position.pmp)
    add_custom_command(OUTPUT ${DUMMY_CALC_IMAGE}
        COMMAND ${ARM_CC} -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -mlong-calls
            -std=c11 -Os -ffreestanding -fno-builtin -fno-common
            -ffunction-sections -fdata-sections -fno-jump-tables
            -Wall -Wextra -Werror
            -I${CMAKE_CURRENT_LIST_DIR}/include
            -I${CMAKE_CURRENT_LIST_DIR}/compat
            -I${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin
            ${CMAKE_CURRENT_LIST_DIR}/calculation-dummy/random_position.c
            ${CMAKE_CURRENT_LIST_DIR}/calculation-dummy/descriptor.c
            ${CMAKE_CURRENT_LIST_DIR}/common/freestanding.c
            -r -nostdlib -nostartfiles -nodefaultlibs
            -T${CMAKE_CURRENT_BINARY_DIR}/plugin_memory.ld
            -o ${DUMMY_CALC_IMAGE}
        DEPENDS calculation-dummy/random_position.c
            calculation-dummy/descriptor.c
            common/freestanding.c
            ${CMAKE_CURRENT_LIST_DIR}/cmake/plugin_memory.ld.in
            ${CMAKE_CURRENT_LIST_DIR}/cmake/plugin_sections.ld)
    add_custom_command(OUTPUT ${DUMMY_CALC_PACKAGE}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/package_plugin.py
            --image ${DUMMY_CALC_IMAGE} --output ${DUMMY_CALC_PACKAGE}
            --plugin-id 4 --name "Calculate position from source"
            --version ${EF800_PLUGIN_VERSION} --interface calculation
            --execution-model user-worker --target 1
            --ram-size 2048 --stack-size 2048
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/verify_package.py
            ${DUMMY_CALC_PACKAGE} --plugin-id 4 --interface calculation
        DEPENDS ${DUMMY_CALC_IMAGE} tools/package_plugin.py tools/verify_package.py)
    add_custom_target(dummy_random_position_plugin ALL DEPENDS ${DUMMY_CALC_PACKAGE})

    if(PM_BUILD_TARGET_RECOVERY_TESTS)
        if(NOT EF800_RECOVERY_TEST_FAULT_MODE MATCHES "^([1-9]|10)$")
            message(FATAL_ERROR "EF800_RECOVERY_TEST_FAULT_MODE must be 1..10")
        endif()
        function(add_recovery_calculation_plugin target plugin_id fault_mode name service)
            set(image ${CMAKE_CURRENT_BINARY_DIR}/${target}.elf)
            set(package ${CMAKE_CURRENT_BINARY_DIR}/${target}.pmp)
            add_custom_command(OUTPUT ${image}
                COMMAND ${ARM_CC} -mcpu=cortex-m4 -mthumb -mfloat-abi=soft
                    -mlong-calls -std=c11 -Os -ffreestanding -fno-builtin
                    -fno-common -ffunction-sections -fdata-sections
                    -fno-jump-tables -Wall -Wextra -Werror
                    -I${CMAKE_CURRENT_LIST_DIR}/include
                    -I${CMAKE_CURRENT_LIST_DIR}/compat
                    -I${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin
                    -DEF800_TEST_PLUGIN_ID=${plugin_id}u
                    -DEF800_TEST_PLUGIN_MODE=${fault_mode}
                    ${CMAKE_CURRENT_LIST_DIR}/recovery-test/calculation_plugin.c
                    ${CMAKE_CURRENT_LIST_DIR}/recovery-test/descriptor.c
                    ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_plugin_worker_sdk.c
                    ${CMAKE_CURRENT_LIST_DIR}/common/freestanding.c
                    -r -nostdlib -nostartfiles -nodefaultlibs
                    -T${CMAKE_CURRENT_BINARY_DIR}/plugin_memory.ld
                    -o ${image}
                DEPENDS recovery-test/calculation_plugin.c
                    recovery-test/descriptor.c
                    ${CMAKE_CURRENT_LIST_DIR}/cmake/plugin_memory.ld.in
                    ${CMAKE_CURRENT_LIST_DIR}/cmake/plugin_sections.ld
                    ${CMAKE_CURRENT_LIST_DIR}/include/ef800_plugin_ids.h
                    ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_plugin_contract.h
                    ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_plugin_metadata.h
                    ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_plugin_worker_abi.h
                    ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_plugin_worker_sdk.c
                    ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_plugin_worker_sdk.h
                    ${CMAKE_CURRENT_LIST_DIR}/common/freestanding.c)
            add_custom_command(OUTPUT ${package}
                COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_LIST_DIR}/tools/package_plugin.py
                    --image ${image} --output ${package}
                    --plugin-id ${plugin_id} --name "${name}"
                    --interface calculation --service ${service}
                    --execution-model user-worker --target 1
                    --ram-size 2048 --stack-size 2048
                COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_LIST_DIR}/tools/verify_package.py
                    ${package} --plugin-id ${plugin_id} --interface calculation
                DEPENDS ${image}
                    ${CMAKE_CURRENT_LIST_DIR}/tools/package_plugin.py
                    ${CMAKE_CURRENT_LIST_DIR}/tools/verify_package.py)
            add_custom_target(${target} ALL DEPENDS ${package})
        endfunction()

        add_recovery_calculation_plugin(recovery_fault_plugin 10
            ${EF800_RECOVERY_TEST_FAULT_MODE}
            "EF800 Fault Probe" none)
        add_recovery_calculation_plugin(recovery_fault_read 12 2
            "EF800 Read Probe" none)
        add_recovery_calculation_plugin(recovery_fault_write 13 3
            "EF800 Write Probe" none)
        add_recovery_calculation_plugin(recovery_fault_xn 14 4
            "EF800 XN Probe" none)
        add_recovery_calculation_plugin(recovery_fault_stack 15 5
            "EF800 Stack Probe" none)
        add_recovery_calculation_plugin(recovery_fault_timeout 16 6
            "EF800 Timeout Probe" none)
        add_recovery_calculation_plugin(recovery_fault_create 17 7
            "EF800 Create Probe" none)
        add_recovery_calculation_plugin(recovery_fault_destroy 18 8
            "EF800 Destroy Probe" none)
        add_recovery_calculation_plugin(recovery_healthy_plugin 11 0
            "EF800 Healthy Probe" none)
        add_recovery_calculation_plugin(recovery_malformed_service 19 9
            "EF800 Malformed Service Probe" worker)
        add_recovery_calculation_plugin(recovery_stale_generation 20 10
            "EF800 Stale Generation Probe" worker)
    endif()
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
                -I${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin
                -DEF800_PLUGIN_ID=${plugin_id}u
                -DEF800_PLUGIN_TARGET_ARCH=2u
                -DEF800_PLUGIN_EXECUTION_MODEL=1u
                ${CMAKE_CURRENT_LIST_DIR}/${source}
                ${CMAKE_CURRENT_LIST_DIR}/common/protocol.c
                ${CMAKE_CURRENT_LIST_DIR}/common/plugin_runtime.c
                ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_plugin_worker_sdk.c
                ${CMAKE_CURRENT_LIST_DIR}/common/freestanding.c
                ${CMAKE_CURRENT_LIST_DIR}/${package_dir}/descriptor.c
                 -r -nostdlib -nostartfiles -nodefaultlibs
                 -Wl,-T,${NATIVE_LINKER_SCRIPT} -o ${image}
             DEPENDS ${source} ${COMMON_SOURCES}
                   ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_plugin_worker_sdk.c
                   ${CMAKE_CURRENT_LIST_DIR}/common/freestanding.c
                  ${CMAKE_CURRENT_LIST_DIR}/include/ef800_plugin_ids.h
                  ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_plugin_metadata.h
                   ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_plugin_worker_abi.h
                   ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_plugin_worker_sdk.h
                   ${package_dir}/descriptor.c ${NATIVE_LINKER_SCRIPT})
         add_custom_command(OUTPUT ${package}
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/package_plugin.py
                 --image ${image} --output ${package} --plugin-id ${plugin_id}
                 --name "${plugin_name}" --version ${EF800_PLUGIN_VERSION} --target 2
                  --execution-model user-worker
                 --ram-size 2048 --stack-size 2048
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/verify_package.py
                ${package} --plugin-id ${plugin_id}
            DEPENDS ${image} tools/package_plugin.py tools/verify_package.py)
        add_custom_target(${target} ALL DEPENDS ${package})
    endfunction()
    add_native_plugin(elta3_native_plugin elta3 elta3/src/elta3.c 2 "EF800 ELTA3 native_sim")
    add_native_plugin(fts3xx_native_plugin fts3xx fts3xx/src/fts3xx.c 3 "EF800 FTS3XX native_sim")

    # Keep package-level native_sim checks in CTest, in addition to the
    # protocol tests above. The build targets generate and verify the PMP
    # images; these tests make architecture/identity verification explicit in
    # the test report used by EF800Application's simulator workflow.
    add_test(NAME elta3_native_sim_package
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/verify_package.py
            ${CMAKE_CURRENT_BINARY_DIR}/elta3_native_plugin.pmp --plugin-id 2)
    add_test(NAME fts3xx_native_sim_package
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/verify_package.py
            ${CMAKE_CURRENT_BINARY_DIR}/fts3xx_native_plugin.pmp --plugin-id 3)

    if(PLUGIN_MANAGER_DIR AND EXISTS "${PLUGIN_MANAGER_DIR}/src/plugin_manager.c")
        add_executable(native_package_runtime_test
            tests/test_native_package.c
            ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_plugin_worker_sdk.c
            ${CMAKE_CURRENT_LIST_DIR}/../../../shared/ef800_plugin/ef800_plugin_worker_abi.c
            ${PLUGIN_MANAGER_DIR}/src/plugin_manager.c
            ${PLUGIN_MANAGER_DIR}/src/plugin_registry.c
            ${PLUGIN_MANAGER_DIR}/src/plugin_package.c
            ${PLUGIN_MANAGER_DIR}/src/plugin_elf.c
            ${PLUGIN_MANAGER_DIR}/src/plugin_descriptor.c
            ${PLUGIN_MANAGER_DIR}/src/plugin_descriptor_bind.c
            ${PLUGIN_MANAGER_DIR}/src/plugin_target_native.c)
        target_include_directories(native_package_runtime_test PRIVATE
            ${PLUGIN_MANAGER_DIR}/include ${COMMON_INCLUDES})
        target_compile_features(native_package_runtime_test PRIVATE c_std_11)
        target_compile_options(native_package_runtime_test PRIVATE
            -m32 -Wall -Wextra -Werror)
        target_link_options(native_package_runtime_test PRIVATE -m32)
        add_dependencies(native_package_runtime_test
            elta3_native_plugin fts3xx_native_plugin)
        add_test(NAME measurement_plugins_native_sim_runtime
            COMMAND native_package_runtime_test
                ${CMAKE_CURRENT_BINARY_DIR}/elta3_native_plugin.pmp
                ${CMAKE_CURRENT_BINARY_DIR}/fts3xx_native_plugin.pmp)
    endif()
endif()
