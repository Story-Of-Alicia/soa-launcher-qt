#include "runtime/GameIntegrityWatcher.hpp"

#include "network/SwiftHttpClient.hpp"
#include "config/Config.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFutureWatcher>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrentRun>
#include <QTimer>
#include <spdlog/spdlog.h>

namespace core::integrity
{
    namespace
    {
        constexpr int k_refresh_delay_ms = 800;
    }

    GameIntegrityWatcher::GameIntegrityWatcher(QObject* parent)
        : QObject(parent),
          watcher(new QFileSystemWatcher(this)),
          network(new core::network::SwiftHttpClient(this)),
          refresh_timer(new QTimer(this))
    {
        refresh_timer->setSingleShot(true);
        refresh_timer->setInterval(k_refresh_delay_ms);
        connect(refresh_timer, &QTimer::timeout, this, &GameIntegrityWatcher::refresh);
        connect(watcher, &QFileSystemWatcher::fileChanged,
                this, &GameIntegrityWatcher::inspect_file);
        connect(watcher, &QFileSystemWatcher::directoryChanged,
                this, &GameIntegrityWatcher::inspect_directory);
        connect(&util::config::Config::instance(), &util::config::Config::changed,
                this, [this]()
        {
            refresh_timer->start();
        });
    }

    int GameIntegrityWatcher::key(const core::game::GameVersion version)
    {
        return version == core::game::GameVersion::Alicia2 ? 2 : 1;
    }

    void GameIntegrityWatcher::set_suspended(const bool value)
    {
        if (suspended == value)
            return;
        suspended = value;
        if (suspended)
        {
            refresh_timer->stop();
            pending_refresh = true;
            clear_watchers();
            return;
        }
        reset_after_suspension();
    }

    void GameIntegrityWatcher::reset_after_suspension()
    {
        pending_refresh = false;
        refresh_timer->start();
    }

    void GameIntegrityWatcher::clear_watchers()
    {
        const QStringList files = watcher->files();
        if (!files.isEmpty())
            watcher->removePaths(files);
        const QStringList directories = watcher->directories();
        if (!directories.isEmpty())
            watcher->removePaths(directories);
        file_versions.clear();
        directory_versions.clear();
    }

    void GameIntegrityWatcher::refresh()
    {
        if (suspended)
        {
            pending_refresh = true;
            return;
        }

        clear_watchers();
        contexts.clear();
        load_context(core::game::GameVersion::Playtest);
        load_context(core::game::GameVersion::Alicia2);
    }

    void GameIntegrityWatcher::load_context(const core::game::GameVersion version)
    {
        auto& config = util::config::Config::instance();
        const QString root = config.game_install_path(version);
        if (root.isEmpty() || !config.path_inside_prefix(root))
            return;

        const QString marker = QDir(root).filePath(
            QString::fromLatin1(core::game::profile(version).install_marker_file));
        QFile file(marker);
        if (!file.open(QIODevice::ReadOnly))
            return;

        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        const QString build = document.object().value(QStringLiteral("version")).toString();
        if (build.isEmpty())
            return;

        Context context;
        context.root = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
        context.version = build;
        contexts.insert(key(version), context);
        fetch_manifest(version, context.root, build);
    }

    void GameIntegrityWatcher::fetch_manifest(const core::game::GameVersion version,
                                              const QString& root,
                                              const QString& build)
    {
        const QString base = QString::fromLatin1(core::game::profile(version).cdn_base_url);
        const QUrl url(QStringLiteral("%1/%2/manifest.json").arg(base, build));
        network->get(
            url,
            15000,
            32 * 1024 * 1024,
            QByteArray("application/json"),
            QByteArray("Story-Of-Alicia-Launcher"),
            false,
            [this, version, root, build](const core::network::HttpResponse& response)
            {
                const bool ok = response.result == soa_http_result_completed
                    && response.status >= 200
                    && response.status < 300
                    && response.data.size() <= 32 * 1024 * 1024;
                auto it = contexts.find(key(version));
                if (!ok || it == contexts.end() || it->root != root || it->version != build)
                    return;
                apply_manifest(version, response.data);
            });
    }

    QString GameIntegrityWatcher::safe_relative_path(const QString& value)
    {
        QString normalized = value;
        normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
        normalized = QDir::cleanPath(normalized);
        if (normalized.isEmpty() || normalized == QStringLiteral(".")
            || normalized.startsWith(QLatin1Char('/'))
            || normalized == QStringLiteral("..")
            || normalized.startsWith(QStringLiteral("../")))
        {
            return {};
        }
        const QStringList parts = normalized.split(QLatin1Char('/'));
        for (const QString& part : parts)
        {
            if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral(".."))
                return {};
        }
        return normalized;
    }

    void GameIntegrityWatcher::apply_manifest(const core::game::GameVersion version,
                                              const QByteArray& payload)
    {
        auto it = contexts.find(key(version));
        if (it == contexts.end() || suspended)
            return;

        const QJsonDocument document = QJsonDocument::fromJson(payload);
        const QJsonArray files = document.object().value(QStringLiteral("files")).toArray();
        if (files.isEmpty())
            return;

        if (files.size() > 50000)
            return;

        it->hashes.clear();
        it->sizes.clear();
        QSet<QString> collisionKeys;
        static const QRegularExpression hashPattern(
            QStringLiteral("^(?:[0-9a-fA-F]{32}|[0-9a-fA-F]{64})$"));
        for (const QJsonValue& value : files)
        {
            const QJsonObject object = value.toObject();
            const QString relative = safe_relative_path(object.value(QStringLiteral("path")).toString());
            const QString hashText = object.value(QStringLiteral("hash")).toString();
            const QByteArray hash = hashText.toLatin1().toLower();
            const qint64 size = object.value(QStringLiteral("size")).toVariant().toLongLong();
            const QString collisionKey = relative.toCaseFolded();
            if (relative.isEmpty() || !hashPattern.match(hashText).hasMatch() || size < 0
                || collisionKeys.contains(collisionKey))
                return;
            collisionKeys.insert(collisionKey);
            it->hashes.insert(relative, hash);
            it->sizes.insert(relative, size);
        }

        if (it->hashes.isEmpty())
            return;

        const int contextKey = key(version);
        const QString root = it->root;
        const QString build = it->version;
        const auto hashes = it->hashes;
        const auto sizes = it->sizes;
        auto* verification = new QFutureWatcher<QStringList>(this);
        connect(verification, &QFutureWatcher<QStringList>::finished, this,
                [this, verification, contextKey, version, root, build]()
        {
            const QStringList changed = verification->result();
            verification->deleteLater();
            auto context = contexts.find(contextKey);
            if (context == contexts.end() || suspended
                || context->root != root || context->version != build)
                return;
            context->ready = true;
            install_watchers(version);
            if (!changed.isEmpty())
                report_change(version, changed);
        });
        verification->setFuture(QtConcurrent::run([root, hashes, sizes]()
        {


            QStringList changed;
            const QDir base(root);
            for (auto file = hashes.cbegin(); file != hashes.cend(); ++file)
            {
                const QString absolute = QDir::cleanPath(base.filePath(file.key()));
                const QFileInfo info(absolute);
                if (!info.exists() || info.size() != sizes.value(file.key(), -1)
                    || GameIntegrityWatcher::hash_file(
                           absolute, file.value().size()) != file.value())
                {
                    changed.append(file.key());
                }
            }
            changed.removeDuplicates();
            return changed;
        }));
    }

    void GameIntegrityWatcher::install_watchers(const core::game::GameVersion version)
    {
        auto it = contexts.find(key(version));
        if (it == contexts.end() || !it->ready)
            return;

        const int contextKey = key(version);
        const QDir root(it->root);
        QSet<QString> requestedFiles;
        QSet<QString> requestedDirectories;
        requestedDirectories.insert(it->root);

        for (auto file = it->hashes.cbegin(); file != it->hashes.cend(); ++file)
        {
            const QString absolute = QDir::cleanPath(root.filePath(file.key()));
            const QFileInfo info(absolute);
            QString directory = info.absolutePath();
            while (directory.startsWith(it->root))
            {
                requestedDirectories.insert(directory);
                if (directory == it->root)
                    break;
                directory = QFileInfo(directory).dir().absolutePath();
            }
            if (info.isFile())
                requestedFiles.insert(absolute);
        }

        for (auto directory = requestedDirectories.begin(); directory != requestedDirectories.end();)
        {
            if (!QFileInfo(*directory).isDir())
                directory = requestedDirectories.erase(directory);
            else
                ++directory;
        }

        const QStringList failedFiles = requestedFiles.isEmpty()
            ? QStringList{} : watcher->addPaths(requestedFiles.values());
        const QStringList failedDirectories = requestedDirectories.isEmpty()
            ? QStringList{} : watcher->addPaths(requestedDirectories.values());
        QSet<QString> failedFileSet;
        for (const QString& path : failedFiles)
            failedFileSet.insert(path);
        QSet<QString> failedDirectorySet;
        for (const QString& path : failedDirectories)
            failedDirectorySet.insert(path);

        for (const QString& path : requestedFiles)
        {
            if (failedFileSet.contains(path))
                continue;
            it->watched_files.insert(path);
            file_versions.insert(path, contextKey);
        }
        for (const QString& path : requestedDirectories)
        {
            if (failedDirectorySet.contains(path))
                continue;
            it->watched_directories.insert(path);
            directory_versions.insert(path, contextKey);
        }

        if (!failedFiles.isEmpty() || !failedDirectories.isEmpty())
        {
            const QString signature = QStringLiteral("%1|%2|%3")
                .arg(it->root).arg(failedFiles.size()).arg(failedDirectories.size());
            if (!reported_watch_failures.contains(signature))
            {
                reported_watch_failures.insert(signature);
                SPDLOG_WARN(
                    "integrity watcher has limited coverage for {}: {} file(s), {} directory path(s) could not be watched",
                    it->root.toStdString(), failedFiles.size(), failedDirectories.size());
            }
        }
    }

    QByteArray GameIntegrityWatcher::hash_file(
        const QString& path, const qsizetype expectedHexLength)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        const QCryptographicHash::Algorithm algorithm = expectedHexLength == 64
            ? QCryptographicHash::Sha256 : QCryptographicHash::Md5;
        QCryptographicHash hash(algorithm);
        if (!hash.addData(&file))
            return {};
        return hash.result().toHex().toLower();
    }

    void GameIntegrityWatcher::inspect_file(const QString& path)
    {
        if (suspended)
        {
            pending_refresh = true;
            return;
        }

        const auto map = file_versions.constFind(QDir::cleanPath(path));
        if (map == file_versions.cend())
            return;
        const int context_key = map.value();
        auto it = contexts.find(context_key);
        if (it == contexts.end() || !it->ready || it->alerted)
            return;

        const QString relative = QDir(it->root).relativeFilePath(path);
        const QFileInfo info(path);
        const auto version = context_key == 2 ? core::game::GameVersion::Alicia2
                                              : core::game::GameVersion::Playtest;
        const qint64 expected_size = it->sizes.value(relative, -1);
        if (!info.exists() || (expected_size >= 0 && info.size() != expected_size))
        {
            report_change(version, {relative});
            return;
        }

        const QByteArray expected_hash = it->hashes.value(relative);
        const QString root = it->root;
        const QString build = it->version;
        auto* verification = new QFutureWatcher<QByteArray>(this);
        connect(verification, &QFutureWatcher<QByteArray>::finished, this,
                [this, verification, context_key, version, root, build, path, relative, expected_hash]()
        {
            const QByteArray actual_hash = verification->result();
            verification->deleteLater();
            auto context = contexts.find(context_key);
            if (context == contexts.end() || suspended || context->alerted
                || context->root != root || context->version != build)
                return;
            if (actual_hash != expected_hash)
            {
                report_change(version, {relative});
                return;
            }
            if (QFileInfo::exists(path) && !watcher->files().contains(path))
            {
                if (watcher->addPath(path))
                {
                    reported_restore_failures.remove(path);
                }
                else if (!reported_restore_failures.contains(path))
                {
                    reported_restore_failures.insert(path);
                    SPDLOG_WARN("integrity watcher could not restore file watch for {}",
                                path.toStdString());
                }
            }
        });
        verification->setFuture(QtConcurrent::run([path, expected_hash]()
        {
            return GameIntegrityWatcher::hash_file(
                path, expected_hash.size());
        }));
    }

    void GameIntegrityWatcher::inspect_directory(const QString& path)
    {
        if (suspended)
        {
            pending_refresh = true;
            return;
        }

        const auto map = directory_versions.constFind(QDir::cleanPath(path));
        if (map == directory_versions.cend())
            return;
        const int context_key = map.value();
        auto it = contexts.find(context_key);
        if (it == contexts.end() || !it->ready || it->alerted)
            return;
        if (it->directory_scan_in_progress)
        {
            it->directory_scan_pending = true;
            return;
        }

        it->directory_scan_in_progress = true;
        const QString root = it->root;
        const QString build = it->version;
        const auto hashes = it->hashes;
        const auto sizes = it->sizes;
        const auto version = context_key == 2 ? core::game::GameVersion::Alicia2
                                              : core::game::GameVersion::Playtest;

        auto* verification = new QFutureWatcher<QStringList>(this);
        connect(verification, &QFutureWatcher<QStringList>::finished, this,
                [this, verification, context_key, version, root, build, path]()
        {
            QStringList changed = verification->result();
            verification->deleteLater();
            auto context = contexts.find(context_key);
            if (context == contexts.end() || context->root != root || context->version != build)
                return;
            context->directory_scan_in_progress = false;
            const bool rescan = context->directory_scan_pending;
            context->directory_scan_pending = false;
            if (suspended || context->alerted)
                return;

            if (!changed.isEmpty())
            {
                changed.removeDuplicates();
                report_change(version, changed);
                return;
            }

            const QDir base(root);
            const QStringList watched = watcher->files();
            for (auto file = context->hashes.cbegin(); file != context->hashes.cend(); ++file)
            {
                const QString absolute = QDir::cleanPath(base.filePath(file.key()));
                if (QFileInfo::exists(absolute) && !watched.contains(absolute))
                {
                    if (watcher->addPath(absolute))
                    {
                        context->watched_files.insert(absolute);
                        file_versions.insert(absolute, context_key);
                        reported_restore_failures.remove(absolute);
                    }
                    else if (!reported_restore_failures.contains(absolute))
                    {
                        reported_restore_failures.insert(absolute);
                        SPDLOG_WARN("integrity watcher could not restore file watch for {}",
                                    absolute.toStdString());
                    }
                }
            }
            if (QFileInfo(path).isDir() && !watcher->directories().contains(path))
            {
                if (watcher->addPath(path))
                {
                    context->watched_directories.insert(path);
                    directory_versions.insert(path, context_key);
                    reported_restore_failures.remove(path);
                }
                else if (!reported_restore_failures.contains(path))
                {
                    reported_restore_failures.insert(path);
                    SPDLOG_WARN("integrity watcher could not restore directory watch for {}",
                                path.toStdString());
                }
            }
            if (rescan)
                QTimer::singleShot(0, this, [this, path]() { inspect_directory(path); });
        });
        verification->setFuture(QtConcurrent::run([root, hashes, sizes]()
        {
            QStringList changed;
            const QDir base(root);
            for (auto file = hashes.cbegin(); file != hashes.cend(); ++file)
            {
                const QString absolute = QDir::cleanPath(base.filePath(file.key()));
                const QFileInfo info(absolute);
                if (!info.exists() || info.size() != sizes.value(file.key(), -1))
                    changed.append(file.key());
            }
            return changed;
        }));
    }

    void GameIntegrityWatcher::report_change(const core::game::GameVersion version,
                                             const QStringList& paths)
    {
        auto it = contexts.find(key(version));
        if (it == contexts.end() || it->alerted)
            return;
        it->alerted = true;
        emit protected_files_changed(version, paths);
    }
}
