# Story of Alicia Launcher
# SPDX-FileCopyrightText: 2026 Story Of Alicia Contributors
# SPDX-License-Identifier: GPL-3.0-only
# Additional terms apply, see ADDITIONAL_TERMS.md.

qt_add_executable(soa_launcher
        main.cpp
        MainWindow.cpp
        MainWindowActions.cpp
        MainWindowEvents.cpp
        MainWindowSetup.cpp
        MainWindowState.cpp
        MainWindowPrivate.hpp
        LauncherMenuController.cpp
        SystemTrayController.cpp
        ${SOA_INCLUDE_DIR}/app/MainWindow.hpp
        ${SOA_INCLUDE_DIR}/app/LauncherMenuController.hpp
        ${SOA_INCLUDE_DIR}/app/SystemTrayController.hpp
        ${SOA_ROOT_DIR}/resources.qrc
)

target_include_directories(soa_launcher
        PRIVATE
        ${SOA_INCLUDE_DIR}
)

target_link_libraries(soa_launcher
        PRIVATE
        soa_common
        soa_config
        soa_i18n
        soa_auth
        soa_credentials
        soa_network_bridge
        soa_runtime
        soa_ui
        soa_update
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
)

soa_apply_build_options(soa_launcher)

set_target_properties(soa_launcher PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

set(SOA_TRANSLATION_FILES
        "${SOA_ROOT_DIR}/translations/soa_launcher_nb.ts"
        "${SOA_ROOT_DIR}/translations/soa_launcher_nl.ts"
)

if(APPLE AND CMAKE_GENERATOR STREQUAL "Xcode")
    get_target_property(soa_lrelease_executable Qt6::lrelease IMPORTED_LOCATION)
    if(NOT soa_lrelease_executable)
        foreach(soa_config RELEASE RELWITHDEBINFO DEBUG MINSIZEREL)
            get_target_property(soa_lrelease_executable Qt6::lrelease "IMPORTED_LOCATION_${soa_config}")
            if(soa_lrelease_executable)
                break()
            endif()
        endforeach()
    endif()
    if(NOT EXISTS "${soa_lrelease_executable}")
        message(FATAL_ERROR "Unable to locate Qt lrelease for Xcode translation generation")
    endif()

    set(soa_translation_dir "${CMAKE_CURRENT_BINARY_DIR}/translations")
    file(MAKE_DIRECTORY "${soa_translation_dir}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SOA_TRANSLATION_FILES})

    set(soa_qm_files)
    foreach(soa_ts_file IN LISTS SOA_TRANSLATION_FILES)
        get_filename_component(soa_translation_name "${soa_ts_file}" NAME_WE)
        set(soa_qm_file "${soa_translation_dir}/${soa_translation_name}.qm")
        execute_process(
                COMMAND "${soa_lrelease_executable}" -nounfinished "${soa_ts_file}" -qm "${soa_qm_file}"
                RESULT_VARIABLE soa_lrelease_result
                OUTPUT_VARIABLE soa_lrelease_output
                ERROR_VARIABLE soa_lrelease_error
        )
        if(NOT soa_lrelease_result EQUAL 0)
            message(FATAL_ERROR "Could not compile ${soa_ts_file}: ${soa_lrelease_output} ${soa_lrelease_error}")
        endif()
        list(APPEND soa_qm_files "${soa_qm_file}")
    endforeach()

    qt_add_resources(soa_launcher "soa_launcher_translations"
            PREFIX "/i18n"
            BASE "${soa_translation_dir}"
            FILES ${soa_qm_files}
    )
elseif(Qt6_VERSION VERSION_GREATER_EQUAL "6.9")
    qt_add_translations(soa_launcher
            TS_FILES ${SOA_TRANSLATION_FILES}
            RESOURCE_PREFIX "/i18n"
            LRELEASE_OPTIONS -nounfinished
            QT_TRANSLATION_CATALOGS qtbase
    )
else()
    qt_add_translations(soa_launcher
            TS_FILES ${SOA_TRANSLATION_FILES}
            RESOURCE_PREFIX "/i18n"
            LRELEASE_OPTIONS -nounfinished
    )
endif()

include("${SOA_ROOT_DIR}/cmake/AppBundle.cmake")
