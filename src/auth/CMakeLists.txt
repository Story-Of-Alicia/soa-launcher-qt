add_library(soa_auth STATIC
        AuthHandler.cpp
        ${SOA_INCLUDE_DIR}/auth/AuthHandler.hpp
)

target_include_directories(soa_auth
        PUBLIC
        ${SOA_INCLUDE_DIR}
)

target_link_libraries(soa_auth
        PUBLIC
        soa_common
        soa_config
        soa_credentials
        soa_platform
        Qt6::Core
        Qt6::Gui
)

soa_apply_build_options(soa_auth)
