add_library(soa_update STATIC
        LauncherUpdateManager.cpp
        LauncherReleaseCatalogue.cpp
        ${SOA_INCLUDE_DIR}/update/LauncherUpdateManager.hpp
        ${SOA_INCLUDE_DIR}/update/LauncherReleaseCatalogue.hpp
)

target_include_directories(soa_update
        PUBLIC
        ${SOA_INCLUDE_DIR}
)

target_link_libraries(soa_update
        PUBLIC
        soa_common
        soa_i18n
        soa_network
        Qt6::Core
)

soa_apply_build_options(soa_update)
