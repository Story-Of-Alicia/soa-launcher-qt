target_sources(soa_common PRIVATE
        Config.cpp
        ${SOA_INCLUDE_DIR}/config/Config.hpp
)

target_link_libraries(soa_common
        PUBLIC
        soa_credentials
)
