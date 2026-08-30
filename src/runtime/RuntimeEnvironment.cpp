#include "runtime/RuntimeLocator.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

#include <spdlog/spdlog.h>

namespace core::wine
{
    bool repair_doubled_proton_prefix(const QString& compat_data_root)
    {
        if (compat_data_root.isEmpty())
            return false;

        const QDir root(compat_data_root);
        const QString prefix = root.filePath(QStringLiteral("pfx"));
        const QString inner = QDir(prefix).filePath(QStringLiteral("pfx"));

        if (!QFileInfo(inner).isDir())
            return false;



        if (QFileInfo(QDir(prefix).filePath(QStringLiteral("drive_c"))).isDir())
            return false;

        const bool innerIsRealPrefix =
            QFileInfo(QDir(inner).filePath(QStringLiteral("drive_c"))).isDir();

        if (!innerIsRealPrefix)
        {

            SPDLOG_INFO("prefix: removing empty doubled prefix at {}", inner.toStdString());
            return QDir(inner).removeRecursively();
        }





        const QString staging = root.filePath(QStringLiteral(".pfx-migrating"));
        if (QFileInfo::exists(staging))
        {
            SPDLOG_WARN("prefix: migration staging path {} already exists; skipping",
                        staging.toStdString());
            return false;
        }

        SPDLOG_INFO("prefix: found doubled Proton prefix; hoisting {} to {}",
                    inner.toStdString(), prefix.toStdString());

        if (!QDir().rename(prefix, staging))
        {
            SPDLOG_ERROR("prefix: could not move {} aside for migration", prefix.toStdString());
            return false;
        }

        const QString stagedInner = QDir(staging).filePath(QStringLiteral("pfx"));
        if (!QDir().rename(stagedInner, prefix))
        {
            SPDLOG_ERROR("prefix: could not hoist {}; restoring original layout",
                         stagedInner.toStdString());
            if (!QDir().rename(staging, prefix))
                SPDLOG_CRITICAL("prefix: could not restore {} from {}",
                                prefix.toStdString(), staging.toStdString());
            return false;
        }

        if (!QDir(staging).removeRecursively())
            SPDLOG_WARN("prefix: hoisted the prefix but could not remove leftover {}",
                        staging.toStdString());

        SPDLOG_INFO("prefix: doubled prefix repaired");
        return true;
    }

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
                 QStringLiteral("STEAM_COMPAT_LAUNCHER_SERVICE"),
                 QStringLiteral("SteamAppId")})
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
        environment.insert(QStringLiteral("PROTONPATH"), proton_root);




        environment.insert(QStringLiteral("WINEPREFIX"),
                           settings.proton_compat_data_root.isEmpty()
                               ? settings.prefix_root
                               : settings.proton_compat_data_root);
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
                upper == QStringLiteral("STORE") || upper == QStringLiteral("STEAMAPPID") ||
                upper == QStringLiteral("STEAMGAMEID") ||
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
