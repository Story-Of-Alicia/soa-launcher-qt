#include "update/LauncherReleaseCatalogue.hpp"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <utility>

namespace core::update
{
    namespace
    {
        constexpr qulonglong k_max_package_size = 1024ULL * 1024ULL * 1024ULL;

        bool valid_hex_digest(const QByteArray& value)
        {
            static const QRegularExpression pattern(QStringLiteral("^[0-9A-Fa-f]{64}$"));
            return pattern.match(QString::fromLatin1(value)).hasMatch();
        }

        bool valid_version(const QString& value)
        {
            static const QRegularExpression pattern(QStringLiteral(
                "^[0-9]+(?:\\.[0-9]+)*(?:-[0-9A-Za-z]+(?:\\.[0-9A-Za-z]+)*)?"
                "(?:\\+[0-9A-Za-z.-]+)?$"));
            return pattern.match(value).hasMatch();
        }

        bool valid_package_url(const QUrl& url, const QString& file_name)
        {
            return url.isValid()
                && url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0
                && QFileInfo(url.path()).fileName() == file_name;
        }
    }

    bool parse_launcher_release_catalogue(const QString& json,
                                          const QString& expected_platform,
                                          QList<LauncherRelease>* releases)
    {
        if (!releases)
            return false;
        releases->clear();

        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
            return false;

        const QJsonObject root = document.object();
        if (root.value(QStringLiteral("schema")).toInt() != 1
            || root.value(QStringLiteral("platform")).toString() != expected_platform)
        {
            return false;
        }

        const QJsonArray entries = root.value(QStringLiteral("releases")).toArray();
        if (entries.isEmpty() || entries.size() > 3)
            return false;

        const QString package_kind = expected_platform == QStringLiteral("macos")
            ? QStringLiteral("dmg")
            : expected_platform == QStringLiteral("linux-x86_64")
                ? QStringLiteral("appimage")
                : QString{};
        const QString extension = package_kind == QStringLiteral("dmg")
            ? QStringLiteral(".dmg")
            : QStringLiteral(".appimage");
        if (package_kind.isEmpty())
            return false;

        QSet<QString> versions;
        for (const QJsonValue& value : entries)
        {
            if (!value.isObject())
            {
                releases->clear();
                return false;
            }

            const QJsonObject entry = value.toObject();
            LauncherRelease release;
            release.version = entry.value(QStringLiteral("version")).toString().trimmed();
            release.minimum_version =
                entry.value(QStringLiteral("minimum_version")).toString().trimmed();
            release.message = entry.value(QStringLiteral("message")).toString();
            release.package_kind = package_kind;
            release.file_name = entry.value(QStringLiteral("file_name")).toString();
            release.url = QUrl(entry.value(QStringLiteral("url")).toString());
            release.sha256 = entry.value(QStringLiteral("sha256")).toString().toLatin1();
            release.released_at = QDateTime::fromString(
                entry.value(QStringLiteral("released_at")).toString(), Qt::ISODate);
            release.size = entry.value(QStringLiteral("size")).toVariant().toULongLong();
            release.required = entry.value(QStringLiteral("required")).toBool(false);

            const QJsonValue mirrors_value = entry.value(QStringLiteral("mirrors"));
            if (!mirrors_value.isUndefined() && !mirrors_value.isArray())
            {
                releases->clear();
                return false;
            }
            const QJsonArray mirror_values = mirrors_value.toArray();
            for (const QJsonValue& mirror_value : mirror_values)
            {
                if (!mirror_value.isString())
                {
                    releases->clear();
                    return false;
                }
                release.mirrors.push_back(QUrl(mirror_value.toString()));
            }

            bool mirrors_valid = true;
            for (const QUrl& mirror : release.mirrors)
                mirrors_valid = mirrors_valid && valid_package_url(mirror, release.file_name);

            if (entry.value(QStringLiteral("schema")).toInt() != 1
                || entry.value(QStringLiteral("platform")).toString() != expected_platform
                || !valid_version(release.version)
                || (!release.minimum_version.isEmpty()
                    && !valid_version(release.minimum_version))
                || versions.contains(release.version)
                || release.file_name.isEmpty()
                || release.file_name.size() > 180
                || QFileInfo(release.file_name).fileName() != release.file_name
                || !release.file_name.endsWith(extension, Qt::CaseInsensitive)
                || !valid_package_url(release.url, release.file_name)
                || !mirrors_valid
                || !valid_hex_digest(release.sha256)
                || !release.released_at.isValid()
                || release.size == 0
                || release.size > k_max_package_size)
            {
                releases->clear();
                return false;
            }

            versions.insert(release.version);
            releases->push_back(std::move(release));
        }
        return true;
    }
}
