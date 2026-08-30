add_library(soa_credentials STATIC
        CredentialStore.cpp
        ${SOA_INCLUDE_DIR}/credentials/CredentialStore.hpp
)

target_include_directories(soa_credentials
        PUBLIC
        ${SOA_INCLUDE_DIR}
)

target_link_libraries(soa_credentials
        PUBLIC
        Qt6::Core
)
