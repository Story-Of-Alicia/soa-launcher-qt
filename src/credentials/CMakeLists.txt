# Story of Alicia Launcher
# SPDX-FileCopyrightText: 2026 Story of Alicia contributors
# SPDX-License-Identifier: GPL-3.0-only
# Additional terms apply, see ADDITIONAL_TERMS.md.

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

soa_apply_build_options(soa_credentials)
