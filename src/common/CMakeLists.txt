add_library(soa_common STATIC
        AppPaths.cpp
        DesktopEntry.cpp
        LaunchArguments.cpp
        Log.cpp
        StatusBus.cpp
        StatusReporter.cpp

        ${SOA_INCLUDE_DIR}/common/AppPaths.hpp
        ${SOA_INCLUDE_DIR}/common/DesktopEntry.hpp
        ${SOA_INCLUDE_DIR}/common/GameVersion.hpp
        ${SOA_INCLUDE_DIR}/common/LaunchArguments.hpp
        ${SOA_INCLUDE_DIR}/common/Log.hpp
        ${SOA_INCLUDE_DIR}/common/QtLogSink.hpp
        ${SOA_INCLUDE_DIR}/common/Status.hpp
        ${SOA_INCLUDE_DIR}/common/StatusBus.hpp
        ${SOA_INCLUDE_DIR}/common/StatusReporter.hpp
)

target_include_directories(soa_common
        PUBLIC
        ${SOA_INCLUDE_DIR}
)

target_link_libraries(soa_common
        PUBLIC
        Qt6::Core
        spdlog::spdlog
)

soa_apply_build_options(soa_common)
