if(APPLE)
    set_target_properties(soa_launcher PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST "${SOA_ROOT_DIR}/packaging/macos/Info.plist.in"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.storyofalicia.launcher"
            MACOSX_BUNDLE_BUNDLE_NAME "Story Of Alicia Launcher"
            MACOSX_BUNDLE_ICON_FILE "soa-launcher.icns"
            BUILD_RPATH "@executable_path/../Frameworks"
            INSTALL_RPATH "@executable_path/../Frameworks"
    )

    set(soa_icon "${SOA_ROOT_DIR}/packaging/macos/soa-launcher.icns")
    set_source_files_properties("${soa_icon}" PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources
    )
    target_sources(soa_launcher PRIVATE "${soa_icon}")

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
endif()
