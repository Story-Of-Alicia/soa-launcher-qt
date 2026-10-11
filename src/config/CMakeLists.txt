add_library(soa_config STATIC
        Config.cpp
        ConfigCredentials.cpp
        ConfigGame.cpp
        ConfigPersistence.cpp
        ConfigPreferences.cpp
        ConfigRuntime.cpp
        ConfigSchema.cpp
        ConfigPrivate.hpp
        ${SOA_INCLUDE_DIR}/config/Config.hpp
)

target_include_directories(soa_config PUBLIC
        ${SOA_INCLUDE_DIR}
)

target_link_libraries(soa_config
        PUBLIC
        soa_common
        Qt6::Core
        PRIVATE
        soa_credentials
        soa_runtime_support
)

soa_apply_build_options(soa_config)
