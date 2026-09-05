#pragma once

#include "config/Config.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QVariantMap>

#include <utility>

#include "common/Log.hpp"
#include "credentials/CredentialStore.hpp"
#include "runtime/MacWineRuntime.hpp"
#include "runtime/WineProcess.hpp"
#include "runtime/WineRegistry.hpp"

#include <spdlog/spdlog.h>

namespace util::config
{
    inline QString absolute_clean_path(const QString& path)
    {
        return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    }

    inline bool path_has_prefix(const QString& candidate, const QString& root)
    {
        return candidate == root || candidate.startsWith(root + QDir::separator());
    }

    inline QString host_wine_user()
    {
        return qEnvironmentVariable(
            "USER", QDir::homePath().section(QLatin1Char('/'), -1));
    }

    inline QString game_path_for_user(const QString& prefix,
                                      const core::game::GameVersion version,
                                      const QString& user)
    {
        const QString folder = QString::fromLatin1(
            core::game::profile(version).default_install_directory);
        return QDir(prefix).filePath(
            QStringLiteral("drive_c/users/%1/AppData/Roaming/%2/game")
                .arg(user, folder));
    }

    inline constexpr int k_integrity_interval_ms = 5000;
    inline constexpr int k_integrity_max_ms = 300000;

    inline QByteArray file_digest(const QString& path)
    {
        const QFileInfo info(path);
        if (!info.exists())
            return QByteArrayLiteral("<missing>");

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return QByteArrayLiteral("<unreadable>");
        return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
    }

    inline QString normalized_launcher_size(const QString& value)
    {
        const QString candidate = value.trimmed().toLower();
        if (candidate == QStringLiteral("900x544"))
            return QStringLiteral("1120x677");

        static const QStringList supported {
            QStringLiteral("1120x677"),
            QStringLiteral("1400x846"),
            QStringLiteral("1600x967"),
            QStringLiteral("1920x1160")
        };
        return supported.contains(candidate)
            ? candidate
            : QStringLiteral("1400x846");
    }

    inline QString normalized_language(const QString& value)
    {
        const QString candidate = value.trimmed().toLower().replace(QLatin1Char('_'), QLatin1Char('-'));
        if (candidate == QStringLiteral("no") || candidate.startsWith(QStringLiteral("no-"))
            || candidate.startsWith(QStringLiteral("nb-")))
            return QStringLiteral("nb");
        if (candidate.startsWith(QStringLiteral("nl-")))
            return QStringLiteral("nl");
        if (candidate.startsWith(QStringLiteral("en-")))
            return QStringLiteral("en");
        if (candidate == QStringLiteral("nb") || candidate == QStringLiteral("nl"))
            return candidate;
        return QStringLiteral("en");
    }

    inline QString normalized_after_launch(const QString& value)
    {
        const QString candidate = value.trimmed().toLower();
        return candidate == QStringLiteral("minimize")
            ? candidate : QStringLiteral("keep");
    }

    inline QString bounded_arguments(const QString& value)
    {
        QString normalized = value;
        normalized.remove(QChar(u'\0'));
        if (normalized.size() > 8192)
            normalized.truncate(8192);
        return normalized;
    }

    class Config::Impl
    {
    public:
        QVariantMap values;
        QString username;
        QString token;
        QString display_name;
        QString persistence_error;
    };
}
