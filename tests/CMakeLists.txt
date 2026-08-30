find_package(Qt6 REQUIRED COMPONENTS Test)

set(SOA_TEST_COMMON_COMPILE_OPTIONS_SOA)
if(SOA_PORTABLE_CXX_OPTIONS)
    set(SOA_TEST_COMMON_COMPILE_OPTIONS_SOA ${SOA_PORTABLE_CXX_OPTIONS})
endif()

# C++ tests

set(SOA_LAUNCHER_TEST_TARGETS
        environment_tests
        launch_argument_tests
        prefix_path_tests
        process_detection_tests
        prefix_inspector_tests
        runtime_locator_tests
        game_profile_tests
)

foreach(soa_test IN LISTS SOA_LAUNCHER_TEST_TARGETS)
    qt_add_executable(${soa_test}
            cpp/${soa_test}.cpp
    )

    target_compile_options(${soa_test} PRIVATE
            ${SOA_TEST_COMMON_COMPILE_OPTIONS_SOA}
    )

    target_link_libraries(${soa_test} PRIVATE
            soa_common
            soa_runtime
            Qt6::Test
            spdlog::spdlog
    )

    set_target_properties(${soa_test} PROPERTIES
            LINKER_LANGUAGE CXX
    )

    target_compile_definitions(${soa_test} PRIVATE
            SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
    )

    add_test(
            NAME ${soa_test}
            COMMAND ${soa_test}
    )
endforeach()

qt_add_executable(launcher_release_catalogue_tests
        cpp/launcher_release_catalogue_tests.cpp
)

target_link_libraries(launcher_release_catalogue_tests PRIVATE
        soa_update
        Qt6::Core
)

add_test(
        NAME launcher_release_catalogue_tests
        COMMAND launcher_release_catalogue_tests
)

# Swift tests

add_executable(courier_path_tests
        swift/courier_paths_tests.swift
        ${CMAKE_SOURCE_DIR}/src/network/CourierPaths.swift
)

target_compile_options(courier_path_tests PRIVATE
        "$<$<COMPILE_LANGUAGE:Swift>:-parse-as-library>"
)

set_target_properties(courier_path_tests PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
        Swift_MODULE_NAME SoaCourierPathTests
)

add_test(
        NAME courier_path_tests
        COMMAND courier_path_tests
)

add_executable(courier_cancel_tests
        swift/courier_cancel_tests.swift
        ${CMAKE_SOURCE_DIR}/src/network/Courier.swift
)

target_compile_options(courier_cancel_tests PRIVATE
        "$<$<COMPILE_LANGUAGE:Swift>:-parse-as-library>"
        "$<$<COMPILE_LANGUAGE:Swift>:SHELL:-Xcc -fmodule-map-file=${CMAKE_SOURCE_DIR}/src/network/module.modulemap>"
)

target_include_directories(courier_cancel_tests PRIVATE
        ${SOA_INCLUDE_DIR}
)

set_target_properties(courier_cancel_tests PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
        Swift_MODULE_NAME SoaCourierCancelTests
)

add_test(
        NAME courier_cancel_tests
        COMMAND courier_cancel_tests
)

add_executable(swift_network_abi_tests
        swift/swift_network_abi_tests.swift
)

target_compile_options(swift_network_abi_tests PRIVATE
        "$<$<COMPILE_LANGUAGE:Swift>:-parse-as-library>"
        "$<$<COMPILE_LANGUAGE:Swift>:SHELL:-Xcc -fmodule-map-file=${CMAKE_SOURCE_DIR}/src/network/module.modulemap>"
)

target_include_directories(swift_network_abi_tests PRIVATE
        ${SOA_INCLUDE_DIR}
)

target_link_libraries(swift_network_abi_tests PRIVATE
        soa_network
)

set_target_properties(swift_network_abi_tests PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
        Swift_MODULE_NAME SoaSwiftNetworkABITests
)

add_test(
        NAME swift_network_abi_tests
        COMMAND swift_network_abi_tests
)

add_executable(courier_hash_tests
        swift/courier_hash_tests.swift
        ${CMAKE_SOURCE_DIR}/src/network/MD5.swift
)

target_compile_options(courier_hash_tests PRIVATE
        "$<$<COMPILE_LANGUAGE:Swift>:-parse-as-library>"
)

set_target_properties(courier_hash_tests PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
        Swift_MODULE_NAME SoaCourierHashTests
)

add_test(
        NAME courier_hash_tests
        COMMAND courier_hash_tests
)

# Seal test

add_executable(soa_seal_tests
        cpp/soa_seal_tests.cpp
)

target_include_directories(soa_seal_tests PRIVATE
        ${SOA_INCLUDE_DIR}
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(soa_seal_tests PRIVATE
            soa_seal_crypto
    )
else()
    target_link_libraries(soa_seal_tests PRIVATE
            soa_network
    )
endif()

set_target_properties(soa_seal_tests PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
)

add_test(
        NAME soa_seal_tests
        COMMAND soa_seal_tests
)