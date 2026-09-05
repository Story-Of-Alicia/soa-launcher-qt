cmake_minimum_required(VERSION 3.21.1)

if(CMAKE_HOST_APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum supported macOS version")
endif()

project(soa_launcher
        LANGUAGES C CXX
        DESCRIPTION "Story of Alicia Launcher"
        VERSION 1.0.0
)

set(SOA_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(SOA_INCLUDE_DIR "${SOA_ROOT_DIR}/include")

include(cmake/ProjectSettings.cmake)
include(cmake/UpdateConfiguration.cmake)
include(cmake/Dependencies.cmake)
include(CTest)

add_subdirectory(src/common)
add_subdirectory(src/credentials)
add_subdirectory(src/config)
add_subdirectory(src/i18n)
add_subdirectory(src/network)
add_subdirectory(src/update)
add_subdirectory(src/runtime)
add_subdirectory(src/platform)
add_subdirectory(src/auth)
add_subdirectory(src/ui)

include(cmake/AliciaLogHook.cmake)

add_subdirectory(src/app)

include(cmake/InstallPackaging.cmake)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
