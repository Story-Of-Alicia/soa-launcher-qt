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
        soa_credentials
        soa_runtime
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
)
