# Optional Windows x86 Alicia diagnostic hook bundled with launcher releases.

set(SOA_ALICIA_LOG_HOOK_SOURCE_DIR
        "${SOA_ROOT_DIR}/third_party/alicia-log-hook")
set(SOA_ALICIA_LOG_HOOK_OUTPUT_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/alicia-log-hook")
set(SOA_ALICIA_LOG_HOOK_INJECTOR
        "${SOA_ALICIA_LOG_HOOK_OUTPUT_DIR}/SoaAliciaLogInjector.exe")
set(SOA_ALICIA_LOG_HOOK_DLL
        "${SOA_ALICIA_LOG_HOOK_OUTPUT_DIR}/SoaAliciaLogHook.dll")

find_program(SOA_MINGW_C_COMPILER NAMES i686-w64-mingw32-gcc)
find_program(SOA_MINGW_CXX_COMPILER NAMES i686-w64-mingw32-g++)

if(SOA_MINGW_C_COMPILER AND SOA_MINGW_CXX_COMPILER)
    file(GLOB_RECURSE SOA_ALICIA_LOG_HOOK_INPUTS CONFIGURE_DEPENDS
            "${SOA_ALICIA_LOG_HOOK_SOURCE_DIR}/src/*"
            "${SOA_ALICIA_LOG_HOOK_SOURCE_DIR}/minhook/include/*"
            "${SOA_ALICIA_LOG_HOOK_SOURCE_DIR}/minhook/src/*"
    )

    find_program(SOA_ALICIA_LOG_HOOK_BASH bash)
    if(NOT SOA_ALICIA_LOG_HOOK_BASH)
        set(SOA_ALICIA_LOG_HOOK_BASH "/bin/bash")
    endif()

    add_custom_command(
            OUTPUT
            "${SOA_ALICIA_LOG_HOOK_INJECTOR}"
            "${SOA_ALICIA_LOG_HOOK_DLL}"
            COMMAND
            "${SOA_ALICIA_LOG_HOOK_BASH}"
            "${SOA_ALICIA_LOG_HOOK_SOURCE_DIR}/build-x86.sh"
            "${SOA_ALICIA_LOG_HOOK_OUTPUT_DIR}"
            "${SOA_MINGW_C_COMPILER}"
            "${SOA_MINGW_CXX_COMPILER}"
            DEPENDS
            ${SOA_ALICIA_LOG_HOOK_INPUTS}
            "${SOA_ALICIA_LOG_HOOK_SOURCE_DIR}/build-x86.sh"
            COMMENT "Building the Windows x86 Alicia injector and compatibility hook"
            VERBATIM
    )

    if(APPLE)
        add_executable(soa_audio_host
                "${SOA_ALICIA_LOG_HOOK_SOURCE_DIR}/src/soa-audio-host.c")
        set_target_properties(soa_audio_host PROPERTIES
                OUTPUT_NAME "soa-audio-host"
                RUNTIME_OUTPUT_DIRECTORY "${SOA_ALICIA_LOG_HOOK_OUTPUT_DIR}")
        target_link_libraries(soa_audio_host PRIVATE
                "-framework AudioToolbox" "-framework CoreFoundation")
    endif()

    add_custom_target(soa_alicia_log_hook ALL
            DEPENDS
            "${SOA_ALICIA_LOG_HOOK_INJECTOR}"
            "${SOA_ALICIA_LOG_HOOK_DLL}"
    )
    add_dependencies(${PROJECT_NAME} soa_alicia_log_hook)
    set(SOA_ALICIA_LOG_HOOK_AVAILABLE ON)
else()
    set(SOA_ALICIA_LOG_HOOK_AVAILABLE OFF)
    if(SOA_REQUIRE_ALICIA_LOG_HOOK)
        message(FATAL_ERROR
                "The Alicia diagnostic hook requires i686-w64-mingw32-gcc and "
                "i686-w64-mingw32-g++")
    endif()
    message(WARNING
            "Windows x86 MinGW was not found; this developer build will not include "
            "the optional Alicia diagnostic log hook")
endif()
