# Story of Alicia Launcher
# SPDX-FileCopyrightText: 2026 Story of Alicia contributors
# SPDX-License-Identifier: GPL-3.0-only
# Additional terms apply, see ADDITIONAL_TERMS.md.

# Runtime layout, install rules, and platform package configuration.

include(GNUInstallDirs)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(soa_network PROPERTIES
            INSTALL_RPATH "$ORIGIN"
    )
    file(RELATIVE_PATH soa_library_relative_path
            "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}"
            "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
    set_target_properties(soa_launcher PROPERTIES
            INSTALL_RPATH "$ORIGIN/${soa_library_relative_path}")
endif()

install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(TARGETS soa_network
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

    install(FILES "${SOA_ROOT_DIR}/packaging/linux/soa-launcher.desktop"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )

    install(FILES "${SOA_ROOT_DIR}/packaging/linux/soa-launcher.png"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
    )

    if(SOA_ALICIA_LOG_HOOK_AVAILABLE)
        install(PROGRAMS "${SOA_ALICIA_LOG_HOOK_INJECTOR}"
                DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/soa-launcher/alicia-log-hook
        )
        install(FILES "${SOA_ALICIA_LOG_HOOK_DLL}"
                DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/soa-launcher/alicia-log-hook
        )
        install(FILES
                "${SOA_ROOT_DIR}/third_party/alicia-log-hook/README.md"
                "${SOA_ALICIA_LOG_HOOK_SOURCE_DIR}/minhook/LICENSE.txt"
                DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/soa-launcher/alicia-log-hook
        )
    endif()
endif()

set(soa_packaging_info
        "version=${PROJECT_VERSION}\nexecutable=$<TARGET_FILE:soa_launcher>\nnetwork=$<TARGET_FILE:soa_network>\nnetwork_name=$<TARGET_FILE_NAME:soa_network>\n")
file(GENERATE
        OUTPUT "${CMAKE_BINARY_DIR}/soa-packaging/$<CONFIG>.txt"
        CONTENT "${soa_packaging_info}")
