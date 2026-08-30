#pragma once

#include "network/SwiftNetwork.h"

#include <QObject>
#include <QString>

namespace core::discord
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
