// Story of Alicia Launcher
// SPDX-FileCopyrightText: 2026 Story Of Alicia Contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional terms apply, see ADDITIONAL_TERMS.md.

#pragma once

#include "network/SwiftNetwork.h"

#include <QObject>
#include <QString>

namespace soa::network
{
    class DiscordRpc final : public QObject
    {
    public:
        explicit DiscordRpc(QObject* parent = nullptr);
        ~DiscordRpc() override;

        void set_launcher_presence();
        void set_game_presence(const QString& username);
        void shutdown();

    private:
        soa_discord_rpc* rpc {};
        bool shutting_down {};
    };
}
