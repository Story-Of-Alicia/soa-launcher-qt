#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QUrl>

namespace core::update
{
    struct LauncherRelease
    {
        QString version;
        QString minimum_version;
        QString message;
        QString package_kind;
        QString file_name;
        QUrl url;
        QList<QUrl> mirrors;
        QByteArray sha256;
        QDateTime released_at;
        qulonglong size {};
        bool required {};
    };

    bool parse_launcher_release_catalogue(const QString& json,
                                          const QString& expected_platform,
                                          QList<LauncherRelease>* releases);
}
