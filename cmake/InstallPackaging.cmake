# Runtime layout, install rules, and platform package configuration.

include(GNUInstallDirs)

if(APPLE)
    set_target_properties(soa_network PROPERTIES
            MACOSX_RPATH TRUE
            INSTALL_NAME_DIR "@rpath"
            BUILD_WITH_INSTALL_NAME_DIR TRUE
    )

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>/Contents/Frameworks"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:soa_network>"
            "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>/Contents/Frameworks/$<TARGET_FILE_NAME:soa_network>"
            VERBATIM
    )

    if(SOA_ALICIA_LOG_HOOK_AVAILABLE)
        if(TARGET soa_audio_host)
            add_dependencies(${PROJECT_NAME} soa_audio_host)
        endif()
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>/Contents/Resources/alicia-log-hook"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SOA_ALICIA_LOG_HOOK_INJECTOR}"
                "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>/Contents/Resources/alicia-log-hook/SoaAliciaLogInjector.exe"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SOA_ALICIA_LOG_HOOK_DLL}"
                "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>/Contents/Resources/alicia-log-hook/SoaAliciaLogHook.dll"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:soa_audio_host>"
                "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>/Contents/Resources/alicia-log-hook/soa-audio-host"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SOA_ROOT_DIR}/third_party/alicia-log-hook/README.md"
                "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>/Contents/Resources/alicia-log-hook/README.md"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SOA_ALICIA_LOG_HOOK_SOURCE_DIR}/minhook/LICENSE.txt"
                "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>/Contents/Resources/alicia-log-hook/MINHOOK_LICENSE.txt"
                VERBATIM
        )
    endif()
else()
    set_target_properties(soa_network PROPERTIES
            INSTALL_RPATH "$ORIGIN"
    )
endif()

install(TARGETS ${PROJECT_NAME}
        BUNDLE DESTINATION .
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(NOT APPLE)
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

set(CPACK_PACKAGE_NAME "Story Of Alicia Launcher")
set(CPACK_PACKAGE_VENDOR "Story Of Alicia")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")

if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "Story Of Alicia Launcher")
    set(CPACK_PACKAGE_FILE_NAME "Story_Of_Alicia-macos")
    include(CPack)
endif()
