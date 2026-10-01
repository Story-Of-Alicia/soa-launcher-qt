#include "runtime/RuntimeLocator.hpp"
#include "runtime/WineRegistry.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

#include <spdlog/spdlog.h>

namespace soa::runtime
{
    QProcessEnvironment RuntimeLocator::make_umu_environment(const RuntimeSettings& settings,
                                                             const QString& proton_root)
    {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        for (const QString& key : {
                 QStringLiteral("STEAM_COMPAT_DATA_PATH"),
                 QStringLiteral("STEAM_COMPAT_CLIENT_INSTALL_PATH"),
                 QStringLiteral("STEAM_COMPAT_APP_ID"),
                 QStringLiteral("STEAM_COMPAT_INSTALL_PATH"),
                 QStringLiteral("STEAM_COMPAT_SHADER_PATH"),
                 QStringLiteral("STEAM_COMPAT_TOOL_PATHS"),
                 QStringLiteral("STEAM_COMPAT_MOUNTS"),
                 QStringLiteral("STEAM_COMPAT_LIBRARY_PATHS"),
                 QStringLiteral("STEAM_COMPAT_LAUNCHER_SERVICE"),
                 QStringLiteral("PROTONPATH"),
                 QStringLiteral("PROTON_VERB"),
                 QStringLiteral("PROTON_USE_WINED3D"),
                 QStringLiteral("UMU_FOLDERS_PATH"),
                 QStringLiteral("UMU_NO_PROTON"),
                 QStringLiteral("UMU_NO_RUNTIME"),
                 QStringLiteral("UMU_RUNTIME_UPDATE"),
                 QStringLiteral("UMU_CONTAINER_NSENTER"),
                 QStringLiteral("UMU_ZENITY"),
                 QStringLiteral("UMU_STEAM_GAME_ID"),
                 QStringLiteral("RUNTIMEPATH"),
                 QStringLiteral("SteamAppId"),
                 QStringLiteral("SteamGameId")})
        {
            environment.remove(key);
        }
        const QString preload = environment.value(QStringLiteral("LD_PRELOAD"));
        if (!preload.isEmpty())
        {
            QStringList retained;
            for (const QString& entry :
                 preload.split(QRegularExpression(QStringLiteral(R"([:\s]+)")),
                               Qt::SkipEmptyParts))
            {
                if (!entry.contains(QStringLiteral("gameoverlayrenderer.so"),
                                    Qt::CaseInsensitive))
                {
                    retained.append(entry);
                }
            }
            if (retained.isEmpty())
                environment.remove(QStringLiteral("LD_PRELOAD"));
            else
                environment.insert(QStringLiteral("LD_PRELOAD"),
                                   retained.join(QLatin1Char(':')));
        }
        environment.insert(QStringLiteral("GAMEID"), QStringLiteral("umu-storyofalicia"));
        environment.insert(QStringLiteral("STORE"), QStringLiteral("none"));

        const QString data_home = managed_umu_data_home();
        if (!data_home.isEmpty())
        {
            QDir().mkpath(data_home);
            environment.insert(QStringLiteral("UMU_FOLDERS_PATH"), data_home);
        }

        if (!is_managed_proton(settings.configured_runtime))
        {
            environment.insert(QStringLiteral("PROTONPATH"), proton_root);
        }
        else
        {
            environment.remove(QStringLiteral("PROTONPATH"));
        }

        environment.insert(QStringLiteral("WINEPREFIX"), settings.prefix_root);
        environment.insert(QStringLiteral("WINEDLLOVERRIDES"), QStringLiteral("winegstreamer="));
        if (!settings.use_dxvk)
            environment.insert(QStringLiteral("PROTON_USE_WINED3D"), QStringLiteral("1"));
        apply_wine_environment_entries(environment, settings.wine_args);
        const QString tmpdir = environment.value(QStringLiteral("TMPDIR"));
        if (!tmpdir.isEmpty() && !QDir(tmpdir).exists())
            environment.remove(QStringLiteral("TMPDIR"));
        return environment;
    }

    void RuntimeLocator::apply_wine_environment_entries(QProcessEnvironment& environment,
                                                        const QString& entries)
    {
        apply_runtime_environment_entries(environment, QProcess::splitCommand(entries));
    }

    void RuntimeLocator::apply_runtime_environment_entries(QProcessEnvironment& environment,
                                                           const QStringList& entries)
    {
        static const QRegularExpression key_pattern(QStringLiteral(R"(^[A-Za-z_][A-Za-z0-9_]*$)"));
        for (const QString& token : entries)
        {
            const int equals = token.indexOf(QLatin1Char('='));
            if (equals <= 0)
            {
                SPDLOG_WARN("ignoring runtime environment entry "
                            "(expected KEY=VALUE): {}",
                            token.toStdString());
                continue;
            }

            const QString key = token.left(equals);
            const QString value = token.mid(equals + 1);
            if (!key_pattern.match(key).hasMatch())
            {
                SPDLOG_WARN("ignoring invalid runtime environment key: {}", key.toStdString());
                continue;
            }

            const QString upper = key.toUpper();
            const bool protected_key =
                upper == QStringLiteral("PATH") || upper == QStringLiteral("HOME") ||
                upper == QStringLiteral("WINE") || upper == QStringLiteral("WINESERVER") ||
                upper == QStringLiteral("WINEPREFIX") || upper == QStringLiteral("WINEARCH") ||
                upper == QStringLiteral("WINEDEBUG") ||
                upper == QStringLiteral("WINEDLLOVERRIDES") ||
                upper == QStringLiteral("LD_PRELOAD") || upper == QStringLiteral("GAMEID") ||
                upper == QStringLiteral("STORE") ||
                upper == QStringLiteral("RUNTIMEPATH") ||
                upper == QStringLiteral("STEAMAPPID") ||
                upper == QStringLiteral("STEAMGAMEID") ||
                upper == QStringLiteral("PROTONPATH") ||
                upper.startsWith(QStringLiteral("UMU_")) ||
                upper.startsWith(QStringLiteral("DYLD_")) ||
                upper.startsWith(QStringLiteral("CX_")) ||
                upper.startsWith(QStringLiteral("PROTON_")) ||
                upper.startsWith(QStringLiteral("STEAM_COMPAT_"));
            if (protected_key)
            {
                SPDLOG_WARN("ignoring launcher-owned runtime "
                            "environment key: {}",
                            key.toStdString());
                continue;
            }

            environment.insert(key, value);
            const bool sensitive = key.contains(QStringLiteral("TOKEN"), Qt::CaseInsensitive) ||
                                   key.contains(QStringLiteral("PASSWORD"), Qt::CaseInsensitive) ||
                                   key.contains(QStringLiteral("SECRET"), Qt::CaseInsensitive);
            SPDLOG_DEBUG("runtime env: {}={}", key.toStdString(),
                         sensitive ? "[REDACTED]" : value.toStdString());
        }
    }
}
