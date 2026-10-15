# Story of Alicia Launcher
# SPDX-FileCopyrightText: 2026 Story Of Alicia Contributors
# SPDX-License-Identifier: GPL-3.0-only
# Additional terms apply, see ADDITIONAL_TERMS.md.

# Third-party dependencies used across launcher modules.

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent LinguistTools)
if(APPLE AND Qt6_VERSION VERSION_LESS 6.9 AND TARGET WrapOpenGL::WrapOpenGL)
    get_target_property(soa_opengl_links WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
    if(soa_opengl_links)
        list(FILTER soa_opengl_links EXCLUDE REGEX "AGL")
        set_property(TARGET WrapOpenGL::WrapOpenGL PROPERTY INTERFACE_LINK_LIBRARIES "${soa_opengl_links}")
    endif()
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
endif()

find_package(spdlog 1.12 QUIET)
if(NOT spdlog_FOUND)
    if(NOT SOA_ALLOW_FETCHCONTENT)
        message(FATAL_ERROR "spdlog was not found and SOA_ALLOW_FETCHCONTENT is OFF")
    endif()

    include(FetchContent)
    FetchContent_Declare(
            spdlog
            GIT_REPOSITORY https://github.com/gabime/spdlog.git
            GIT_TAG v1.16.0
            GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(spdlog)
endif()

if(SOA_PORTABLE_CXX_OPTIONS AND TARGET spdlog)
    target_compile_options(spdlog PRIVATE
            ${SOA_PORTABLE_CXX_OPTIONS}
    )
endif()
