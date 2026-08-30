qt_add_executable(soa_launcher
        main.cpp
        MainWindow.cpp
        ${SOA_INCLUDE_DIR}/app/MainWindow.hpp
        ${SOA_ROOT_DIR}/src/res/resources.qrc
)

target_include_directories(soa_launcher
        PRIVATE
        ${SOA_INCLUDE_DIR}
)

target_link_libraries(soa_launcher
        PRIVATE
        soa_common
        soa_auth
        soa_credentials
        soa_network
        soa_runtime
        soa_ui
        soa_update
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)
