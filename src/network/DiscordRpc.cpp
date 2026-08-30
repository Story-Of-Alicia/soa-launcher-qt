#include "network/DiscordRpc.hpp"

#include <QCoreApplication>

namespace core::discord
{
    namespace
    {
        constexpr auto k_application_id = "1431526738881548369";
    }

    DiscordRpc::DiscordRpc(QObject* parent)
        : QObject(parent)
    {
        const QString configured = qEnvironmentVariable("SOA_RPC_PROXY_URL").trimmed();
        const QByteArray proxy = configured.isEmpty()
            ? QByteArray("http://127.0.0.1:18080")
            : configured.toUtf8();
        rpc = soa_discord_rpc_create(
            k_application_id,
            QCoreApplication::applicationPid(),
            proxy.constData());
    }

    DiscordRpc::~DiscordRpc()
    {
        shutdown();
        if (rpc)
        {
            soa_discord_rpc_destroy(rpc);
            rpc = nullptr;
        }
    }

    void DiscordRpc::set_launcher_presence()
    {
        if (!rpc || shutting_down)
            return;
        soa_discord_rpc_set_launcher_presence(rpc);
    }

    void DiscordRpc::set_game_presence(const QString& username)
    {
        if (!rpc || shutting_down)
            return;
        const QByteArray encoded = username.toUtf8();
        soa_discord_rpc_set_game_presence(rpc, encoded.constData());
    }

    void DiscordRpc::shutdown()
    {
        if (!rpc || shutting_down)
            return;
        shutting_down = true;
        soa_discord_rpc_shutdown(rpc);
    }
}
