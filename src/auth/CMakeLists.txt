# Story of Alicia Launcher
# SPDX-FileCopyrightText: 2026 Story of Alicia contributors
# SPDX-License-Identifier: GPL-3.0-only
# Additional terms apply, see ADDITIONAL_TERMS.md.

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
