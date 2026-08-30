#include "update/LauncherReleaseCatalogue.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{
    QJsonObject release(const QString& version,
                        const QString& file_name = QStringLiteral("Story_Of_Alicia-x86_64.AppImage"),
                        const QString& url = QStringLiteral(
                            "https://r2.storyofalicia.com/launcher/linux/Story_Of_Alicia-x86_64.AppImage"))
    {
        return {
            {QStringLiteral("schema"), 1},
            {QStringLiteral("platform"), QStringLiteral("linux-x86_64")},
            {QStringLiteral("version"), version},
            {QStringLiteral("minimum_version"), QStringLiteral("0.9.0")},
            {QStringLiteral("message"), QStringLiteral("Test release")},
            {QStringLiteral("file_name"), file_name},
            {QStringLiteral("url"), url},
            {QStringLiteral("mirrors"), QJsonArray{
                QStringLiteral("https://github.com/Story-Of-Alicia/soa-launcher-qt/releases/"
                               "download/v1.0.0/Story_Of_Alicia-x86_64.AppImage")}},
            {QStringLiteral("sha256"), QString(64, QLatin1Char('a'))},
            {QStringLiteral("size"), 128 * 1024 * 1024},
            {QStringLiteral("released_at"), QStringLiteral("2026-08-01T12:00:00Z")},
            {QStringLiteral("required"), true}
        };
    }

    QString catalogue(const QJsonArray& entries,
                      const QString& platform = QStringLiteral("linux-x86_64"))
    {
        return QString::fromUtf8(QJsonDocument(QJsonObject{
            {QStringLiteral("schema"), 1},
            {QStringLiteral("platform"), platform},
            {QStringLiteral("releases"), entries}
        }).toJson(QJsonDocument::Compact));
    }

    bool accepts(const QJsonArray& entries)
    {
        QList<core::update::LauncherRelease> releases;
        return core::update::parse_launcher_release_catalogue(
            catalogue(entries), QStringLiteral("linux-x86_64"), &releases);
    }
}

int main()
{
    QList<core::update::LauncherRelease> parsed;
    const QJsonArray valid_entries{
        release(QStringLiteral("1.0.0")),
        release(QStringLiteral("0.9.0")),
        release(QStringLiteral("0.8.0"))
    };
    if (!core::update::parse_launcher_release_catalogue(
            catalogue(valid_entries), QStringLiteral("linux-x86_64"), &parsed)
        || parsed.size() != 3
        || parsed.constFirst().minimum_version != QStringLiteral("0.9.0")
        || !parsed.constFirst().required
        || parsed.constFirst().mirrors.size() != 1)
    {
        return 1;
    }

    if (accepts(QJsonArray{release(QStringLiteral("1.0.0")),
                           release(QStringLiteral("1.0.0"))}))
        return 2;
    if (accepts(QJsonArray{release(QStringLiteral("1.0.0"),
                                          QStringLiteral("../launcher.AppImage"))}))
        return 3;
    if (accepts(QJsonArray{release(QStringLiteral("1.0.0"),
                                          QStringLiteral("launcher.AppImage"),
                                          QStringLiteral("http://example.com/launcher.AppImage"))}))
        return 4;
    if (accepts(QJsonArray{release(QStringLiteral("1.0.0"),
                                          QStringLiteral("launcher.AppImage"),
                                          QStringLiteral("https://example.com/other.AppImage"))}))
        return 5;

    QJsonObject invalid_digest = release(QStringLiteral("1.0.0"));
    invalid_digest.insert(QStringLiteral("sha256"), QStringLiteral("invalid"));
    if (accepts(QJsonArray{invalid_digest}))
        return 6;

    QJsonObject oversized = release(QStringLiteral("1.0.0"));
    oversized.insert(QStringLiteral("size"), 1024.0 * 1024.0 * 1024.0 + 1.0);
    if (accepts(QJsonArray{oversized}))
        return 7;

    if (accepts(QJsonArray{
            release(QStringLiteral("1.0.0")),
            release(QStringLiteral("0.9.0")),
            release(QStringLiteral("0.8.0")),
            release(QStringLiteral("0.7.0"))}))
        return 8;

    if (core::update::parse_launcher_release_catalogue(
            catalogue(QJsonArray{release(QStringLiteral("1.0.0"))},
                      QStringLiteral("macos")),
            QStringLiteral("linux-x86_64"), &parsed))
        return 9;

    return 0;
}
