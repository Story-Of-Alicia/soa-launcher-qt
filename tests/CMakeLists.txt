find_package(Qt6 REQUIRED COMPONENTS Test)

set(SOA_TEST_COMMON_COMPILE_OPTIONS_SOA)
if(SOA_PORTABLE_CXX_OPTIONS)
    set(SOA_TEST_COMMON_COMPILE_OPTIONS_SOA ${SOA_PORTABLE_CXX_OPTIONS})
endif()

set_property(GLOBAL PROPERTY SOA_ENABLED_TEST_TARGETS "")

function(soa_register_test target labels timeout)
    add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
    set_tests_properties(${target} PROPERTIES LABELS "${labels}" TIMEOUT ${timeout})
    set_property(GLOBAL APPEND PROPERTY SOA_ENABLED_TEST_TARGETS ${target})
endfunction()

function(soa_split_csv value output)
    if("${value}" STREQUAL "-")
        set(${output} "" PARENT_SCOPE)
        return()
    endif()
    string(REPLACE "," ";" values "${value}")
    set(${output} ${values} PARENT_SCOPE)
endfunction()

function(soa_sources_exist result)
    set(all_exist TRUE)
    foreach(source ${ARGN})
        if(IS_ABSOLUTE "${source}")
            set(path "${source}")
        else()
            set(path "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
        endif()
        if(NOT EXISTS "${path}")
            set(all_exist FALSE)
        endif()
    endforeach()
    set(${result} ${all_exist} PARENT_SCOPE)
endfunction()

function(soa_add_cpp_test record)
    string(REPLACE "|" ";" fields "${record}")
    list(GET fields 0 target)
    list(GET fields 1 kind)
    list(GET fields 2 sources_csv)
    list(GET fields 3 libraries_csv)
    list(GET fields 4 labels_csv)
    list(GET fields 5 timeout)

    soa_split_csv("${sources_csv}" sources)
    soa_split_csv("${libraries_csv}" libraries)
    string(REPLACE "," ";" labels "${labels_csv}")
    soa_sources_exist(sources_exist ${sources})
    if(NOT sources_exist)
        return()
    endif()

    if(kind STREQUAL "qt")
        qt_add_executable(${target} ${sources})
        target_compile_options(${target} PRIVATE ${SOA_TEST_COMMON_COMPILE_OPTIONS_SOA})
        target_link_libraries(${target} PRIVATE Qt6::Test spdlog::spdlog ${libraries})
        target_compile_definitions(${target} PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
        set_target_properties(${target} PROPERTIES LINKER_LANGUAGE CXX)
    elseif(kind STREQUAL "qtplain")
        qt_add_executable(${target} ${sources})
        target_link_libraries(${target} PRIVATE ${libraries})
    endif()

    soa_register_test(${target} "${labels}" ${timeout})
endfunction()

function(soa_add_swift_test record)
    string(REPLACE "|" ";" fields "${record}")
    list(GET fields 0 target)
    list(GET fields 1 sources_csv)
    list(GET fields 2 libraries_csv)
    list(GET fields 3 labels_csv)
    list(GET fields 4 timeout)
    list(GET fields 5 module)
    list(GET fields 6 modulemap)

    soa_split_csv("${sources_csv}" sources)
    soa_split_csv("${libraries_csv}" libraries)
    string(REPLACE "," ";" labels "${labels_csv}")
    soa_sources_exist(sources_exist ${sources})
    if(NOT sources_exist)
        return()
    endif()

    add_executable(${target} ${sources})
    target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:Swift>:-parse-as-library>")

    if(modulemap)
        target_compile_options(${target} PRIVATE
                "$<$<COMPILE_LANGUAGE:Swift>:SHELL:-Xcc -fmodule-map-file=${CMAKE_SOURCE_DIR}/src/network/module.modulemap>"
        )
        target_include_directories(${target} PRIVATE ${SOA_INCLUDE_DIR})
    endif()

    if(libraries)
        target_link_libraries(${target} PRIVATE ${libraries})
    endif()

    set_target_properties(${target} PROPERTIES
            AUTOMOC OFF
            AUTORCC OFF
            AUTOUIC OFF
            Swift_MODULE_NAME ${module}
    )
    soa_register_test(${target} "${labels}" ${timeout})
endfunction()

set(SOA_CPP_TESTS
        "environment_tests|qt|cpp/environment_tests.cpp|soa_runtime|runtime,security|15"
        "launch_argument_tests|qt|cpp/launch_argument_tests.cpp|soa_runtime|launch,security|15"
        "prefix_path_tests|qt|cpp/prefix_path_tests.cpp|soa_runtime|prefix,security|15"
        "process_detection_tests|qt|cpp/process_detection_tests.cpp|soa_runtime|runtime,process|15"
        "prefix_inspector_tests|qt|cpp/prefix_inspector_tests.cpp|soa_runtime|prefix,dxvk|15"
        "runtime_locator_tests|qt|cpp/runtime_locator_tests.cpp|soa_runtime|runtime,umu,proton|20"
        "ui_layout_tests|qt|cpp/ui_layout_tests.cpp,${SOA_ROOT_DIR}/resources.qrc|soa_ui|ui,layout|45"
        "launcher_release_catalogue_tests|qtplain|cpp/launcher_release_catalogue_tests.cpp|soa_update,Qt6::Core|update,security|10"
)

foreach(record IN LISTS SOA_CPP_TESTS)
    soa_add_cpp_test("${record}")
endforeach()

if(TARGET ui_layout_tests)
    set(SOA_UI_TEST_OUTPUT_DIR "${CMAKE_BINARY_DIR}/test-output/ui-layout")
    file(MAKE_DIRECTORY "${SOA_UI_TEST_OUTPUT_DIR}")
    set_property(TEST ui_layout_tests APPEND PROPERTY ENVIRONMENT "SOA_UI_TEST_OUTPUT_DIR=${SOA_UI_TEST_OUTPUT_DIR}")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_property(TEST ui_layout_tests APPEND PROPERTY ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()

set(SOA_SWIFT_TESTS
        "courier_path_tests|swift/courier_paths_tests.swift,${CMAKE_SOURCE_DIR}/src/network/CourierPaths.swift|-|integrity,security|10|SoaCourierPathTests|FALSE"
        "courier_cancel_tests|swift/courier_cancel_tests.swift,${CMAKE_SOURCE_DIR}/src/network/Courier.swift|-|integrity,cancellation|5|SoaCourierCancelTests|TRUE"
        "swift_network_abi_tests|swift/swift_network_abi_tests.swift|soa_network|network,abi,security|10|SoaSwiftNetworkABITests|TRUE"
        "courier_hash_tests|swift/courier_hash_tests.swift,${CMAKE_SOURCE_DIR}/src/network/MD5.swift|-|integrity,hashing|10|SoaCourierHashTests|FALSE"
)

foreach(record IN LISTS SOA_SWIFT_TESTS)
    soa_add_swift_test("${record}")
endforeach()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cpp/soa_seal_tests.cpp")
    add_executable(soa_seal_tests cpp/soa_seal_tests.cpp)
    target_include_directories(soa_seal_tests PRIVATE ${SOA_INCLUDE_DIR})
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(soa_seal_tests PRIVATE soa_seal_crypto)
    else()
        target_link_libraries(soa_seal_tests PRIVATE soa_network)
    endif()
    set_target_properties(soa_seal_tests PROPERTIES AUTOMOC OFF AUTORCC OFF AUTOUIC OFF)
    soa_register_test(soa_seal_tests "update,signature,security" 10)
endif()

get_property(SOA_ENABLED_TEST_TARGETS GLOBAL PROPERTY SOA_ENABLED_TEST_TARGETS)

add_custom_target(check
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --no-tests=error
        DEPENDS ${SOA_ENABLED_TEST_TARGETS}
        USES_TERMINAL
)
