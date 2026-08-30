# Third-party dependencies used across launcher modules.

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent LinguistTools)
find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)

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
