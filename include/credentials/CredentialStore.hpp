#pragma once

#include <QString>

namespace util::credentials
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
