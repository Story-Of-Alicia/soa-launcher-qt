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
        soa_network
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
        "${SOA_ROOT_DIR}/translations/soa_launcher_en.ts"
        "${SOA_ROOT_DIR}/translations/soa_launcher_nb.ts"
        "${SOA_ROOT_DIR}/translations/soa_launcher_nl.ts"
)

if(Qt6_VERSION VERSION_GREATER_EQUAL "6.9")
    qt_add_translations(soa_launcher
            TS_FILES ${SOA_TRANSLATION_FILES}
            RESOURCE_PREFIX "/i18n"
            QT_TRANSLATION_CATALOGS qtbase
    )
else()
    qt_add_translations(soa_launcher
            TS_FILES ${SOA_TRANSLATION_FILES}
            RESOURCE_PREFIX "/i18n"
    )
endif()

include("${SOA_ROOT_DIR}/cmake/AppBundle.cmake")
