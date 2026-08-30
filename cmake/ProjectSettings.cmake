# Global project/build settings shared by every launcher module.

if(NOT APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "soa_launcher currently supports Linux and macOS only")
endif()

if(NOT CMAKE_GENERATOR MATCHES "^Ninja" AND NOT CMAKE_GENERATOR STREQUAL "Xcode")
    message(FATAL_ERROR "The Swift Courier library requires the Ninja or Xcode generator")
endif()

enable_language(Swift)
set(CMAKE_Swift_LANGUAGE_VERSION 5)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(SOA_ALLOW_FETCHCONTENT
        "Allow downloading missing dependencies during configure" ON)
option(SOA_REQUIRE_ALICIA_LOG_HOOK
        "Fail configuration unless the Windows x86 Alicia diagnostic hook can be built"
        OFF)
option(SOA_PORTABLE_BUILD
        "Use conservative x86-64 compiler settings for distributable Linux builds" ON)

set(SOA_PORTABLE_CXX_OPTIONS)
if(SOA_PORTABLE_BUILD
        AND CMAKE_SYSTEM_NAME STREQUAL "Linux"
        AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$"
        AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    list(APPEND SOA_PORTABLE_CXX_OPTIONS
            -march=x86-64
            -mtune=generic
    )
endif()
