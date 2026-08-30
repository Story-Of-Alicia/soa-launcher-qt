# Config still probes runtime details internally. Keep it in soa_common for this
# structural pass; it can become an independent target after those responsibilities
# are separated from Config itself.
target_sources(soa_common PRIVATE
        Config.cpp
        ${SOA_INCLUDE_DIR}/config/Config.hpp
)

target_link_libraries(soa_common
        PUBLIC
        soa_credentials
)
