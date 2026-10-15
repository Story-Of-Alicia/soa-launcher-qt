// Story of Alicia Launcher
// SPDX-FileCopyrightText: 2026 Story Of Alicia Contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional terms apply, see ADDITIONAL_TERMS.md.

#pragma once

#include <QString>

namespace soa::credentials
{
    struct Credentials
    {
        QString user;
        QString token;
        QString display_name;

        [[nodiscard]] bool valid() const { return !user.isEmpty() && !token.isEmpty(); }
    };

    class CredentialStore
    {
    public:
        static bool load(Credentials& credentials);
        static bool save(const Credentials& credentials);
        static bool clear();
        static bool available();
    };
}
