add_library(soa_platform STATIC
        UrlSchemeHandler.cpp
        ${SOA_INCLUDE_DIR}/platform/UrlSchemeHandler.hpp
)

target_include_directories(soa_platform
        PUBLIC
        ${SOA_INCLUDE_DIR}
)

target_link_libraries(soa_platform
        PUBLIC
        soa_common
        Qt6::Core
        spdlog::spdlog
)

soa_apply_build_options(soa_platform)
