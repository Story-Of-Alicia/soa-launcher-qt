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

if(APPLE)
    find_library(SOA_SECURITY_FRAMEWORK Security REQUIRED)
    find_library(SOA_COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    target_link_libraries(soa_credentials PRIVATE
            ${SOA_SECURITY_FRAMEWORK}
            ${SOA_COREFOUNDATION_FRAMEWORK}
    )
endif()

soa_apply_build_options(soa_credentials)
