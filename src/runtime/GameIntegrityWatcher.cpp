#include "runtime/GameIntegrityWatcher.hpp"

#include "network/SwiftHttpClient.hpp"
#include "config/Config.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
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

namespace soa::runtime
{
    namespace
    {
        constexpr int k_refresh_delay_ms = 800;

        QString path_key(QString value)
        {
            value.replace(QLatin1Char('\\'), QLatin1Char('/'));
            return value.toCaseFolded();
        }

        QString tagged_change(const char* kind, const QString& relative)
        {
            return QString::fromLatin1(kind) + QLatin1Char(':') + relative;
        }

        bool is_internal_launcher_path(const QString& relative)
        {
            const QString first = relative.section(QLatin1Char('/'), 0, 0).toLower();
            return first == QStringLiteral(".soa-update-staging")
                || first == QStringLiteral(".soa-update-backup")
                || first == QStringLiteral(".soa-update-journal.json")
                || first == QStringLiteral(".soa-managed-manifest.json")
                || first == QStringLiteral(".soa-update-staging.json")
                || first.startsWith(QStringLiteral(".soa-recovery-quarantine-"));
        }

        QStringList unexpected_files(const QString& root,
                                     const QHash<QString, QByteArray>& hashes)
        {
            QSet<QString> expected;
            for (auto it = hashes.cbegin(); it != hashes.cend(); ++it)
                expected.insert(path_key(it.key()));

            const QSet<QString> allowed {
                path_key(QStringLiteral("version.json")),
                path_key(QStringLiteral("alice.cfg")),
                path_key(QStringLiteral("alice.cfg.soa-macos-backup"))
            };

            const bool dxvk_active = QFileInfo(
                QDir(root).filePath(QStringLiteral("d3d9.dll"))).isFile()
                && (expected.contains(path_key(QStringLiteral("d3dx9_31.dll.bak")))
                    || expected.contains(path_key(QStringLiteral("d3dx9_42.dll.bak"))));
            const QSet<QString> dxvk_allowed = dxvk_active
                ? QSet<QString> {
                    path_key(QStringLiteral("d3d9.dll")),
                    path_key(QStringLiteral("d3dx9_31.dll")),
                    path_key(QStringLiteral("d3dx9_42.dll"))
                  }
                : QSet<QString> {};

            QStringList unexpected;
            QDirIterator iterator(
                root,
                QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                QDirIterator::Subdirectories);
            const QDir base(root);
            while (iterator.hasNext())
            {
                iterator.next();
                const QFileInfo info = iterator.fileInfo();
                const QString relative = base.relativeFilePath(info.absoluteFilePath())
                    .replace(QLatin1Char('\\'), QLatin1Char('/'));
                if (relative.isEmpty() || is_internal_launcher_path(relative))
                    continue;

                if (info.isSymLink())
                {
                    unexpected.append(relative);
                    continue;
                }
                if (info.isDir())
                    continue;

                const QString key = path_key(relative);
                if (!expected.contains(key) && !allowed.contains(key)
                    && !dxvk_allowed.contains(key))
                {
                    unexpected.append(relative);
                }
            }
            return unexpected;
        }
    }

    GameIntegrityWatcher::GameIntegrityWatcher(QObject* parent)
        : QObject(parent),
          watcher(new QFileSystemWatcher(this)),
          network(new soa::network::SwiftHttpClient(this)),
          refresh_timer(new QTimer(this))
    {
        refresh_timer->setSingleShot(true);
        refresh_timer->setInterval(k_refresh_delay_ms);
        connect(refresh_timer, &QTimer::timeout, this, &GameIntegrityWatcher::refresh);
        connect(watcher, &QFileSystemWatcher::fileChanged,
                this, &GameIntegrityWatcher::inspect_file);
        connect(watcher, &QFileSystemWatcher::directoryChanged,
                this, &GameIntegrityWatcher::inspect_directory);
        connect(&soa::config::Config::instance(), &soa::config::Config::changed,
                this, [this]()
        {
            refresh_timer->start();
        });
    }

    int GameIntegrityWatcher::key(const soa::common::game::GameVersion version)
    {
        return version == soa::common::game::GameVersion::Alicia2 ? 2 : 1;
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
            ++generation;
            contexts.clear();
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

        refresh_timer->stop();
        ++generation;
        clear_watchers();
        contexts.clear();
        load_context(soa::common::game::GameVersion::Playtest);
        load_context(soa::common::game::GameVersion::Alicia2);
    }

    void GameIntegrityWatcher::load_context(const soa::common::game::GameVersion version)
    {
        auto& config = soa::config::Config::instance();
        const QString root = config.game_install_path(version);
        if (root.isEmpty() || !config.path_inside_prefix(root))
            return;

        const QString marker = QDir(root).filePath(
            QString::fromLatin1(soa::common::game::profile(version).install_marker_file));
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

    void GameIntegrityWatcher::fetch_manifest(const soa::common::game::GameVersion version,
                                              const QString& root,
                                              const QString& build)
    {
        const QString base = QString::fromLatin1(soa::common::game::profile(version).cdn_base_url);
        const QUrl url(QStringLiteral("%1/%2/manifest.json").arg(base, build));
        const quint64 scan_generation = generation;
        network->get(
            url,
            15000,
            32 * 1024 * 1024,
            QByteArray("application/json"),
            QByteArray("Story-Of-Alicia-Launcher"),
            false,
            [this, version, root, build, scan_generation](const soa::network::HttpResponse& response)
            {
                const bool ok = response.result == soa_http_result_completed
                    && response.status >= 200
                    && response.status < 300
                    && response.data.size() <= 32 * 1024 * 1024;
                auto it = contexts.find(key(version));
                if (scan_generation != generation || suspended || !ok
                    || it == contexts.end() || it->root != root || it->version != build)
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

    void GameIntegrityWatcher::apply_manifest(const soa::common::game::GameVersion version,
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

        if (QFileInfo(QDir(it->root).filePath(QStringLiteral("d3d9.dll"))).isFile())
        {
            for (const QString& name : {QStringLiteral("d3dx9_31.dll"),
                                        QStringLiteral("d3dx9_42.dll")})
            {
                if (!it->hashes.contains(name))
                    continue;
                const QByteArray hash = it->hashes.take(name);
                const qint64 size = it->sizes.take(name);
                it->hashes.insert(name + QStringLiteral(".bak"), hash);
                it->sizes.insert(name + QStringLiteral(".bak"), size);
            }
        }

        const int contextKey = key(version);
        const QString root = it->root;
        const QString build = it->version;
        const auto hashes = it->hashes;
        const auto sizes = it->sizes;
        const quint64 scan_generation = generation;
        auto* verification = new QFutureWatcher<QStringList>(this);
        connect(verification, &QFutureWatcher<QStringList>::finished, this,
                [this, verification, contextKey, version, root, build, scan_generation]()
        {
            const QStringList changed = verification->result();
            verification->deleteLater();
            auto context = contexts.find(contextKey);
            if (scan_generation != generation || context == contexts.end() || suspended
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
                if (!info.exists())
                {
                    changed.append(tagged_change("missing", file.key()));
                }
                else if (info.size() != sizes.value(file.key(), -1)
                         || GameIntegrityWatcher::hash_file(
                                absolute, file.value().size()) != file.value())
                {
                    changed.append(tagged_change("modified", file.key()));
                }
            }
            for (const QString& path : unexpected_files(root, hashes))
                changed.append(tagged_change("unexpected", path));
            changed.removeDuplicates();
            return changed;
        }));
    }

    void GameIntegrityWatcher::install_watchers(const soa::common::game::GameVersion version)
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

        QDirIterator directoryIterator(
            it->root, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
            QDirIterator::Subdirectories);
        while (directoryIterator.hasNext())
        {
            const QString directory = QDir::cleanPath(directoryIterator.next());
            const QString relative = root.relativeFilePath(directory)
                .replace(QLatin1Char('\\'), QLatin1Char('/'));
            if (!is_internal_launcher_path(relative))
                requestedDirectories.insert(directory);
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
        if (it == contexts.end() || !it->ready)
            return;

        const QString relative = QDir(it->root).relativeFilePath(path);
        const QFileInfo info(path);
        const auto version = context_key == 2 ? soa::common::game::GameVersion::Alicia2
                                              : soa::common::game::GameVersion::Playtest;
        const qint64 expected_size = it->sizes.value(relative, -1);
        if (!info.exists())
        {
            report_change(version, {tagged_change("missing", relative)});
            return;
        }
        if (expected_size >= 0 && info.size() != expected_size)
        {
            report_change(version, {tagged_change("modified", relative)});
            return;
        }

        const QByteArray expected_hash = it->hashes.value(relative);
        const QString root = it->root;
        const QString build = it->version;
        const quint64 scan_generation = generation;
        auto* verification = new QFutureWatcher<QByteArray>(this);
        connect(verification, &QFutureWatcher<QByteArray>::finished, this,
                [this, verification, context_key, version, root, build, path, relative, expected_hash, scan_generation]()
        {
            const QByteArray actual_hash = verification->result();
            verification->deleteLater();
            auto context = contexts.find(context_key);
            if (scan_generation != generation || context == contexts.end() || suspended
                || context->root != root || context->version != build)
                return;
            if (actual_hash != expected_hash)
            {
                report_change(version, {tagged_change("modified", relative)});
                return;
            }
            context->reported_changes.remove(tagged_change("missing", relative));
            context->reported_changes.remove(tagged_change("modified", relative));
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
        if (it == contexts.end() || !it->ready)
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
        const auto version = context_key == 2 ? soa::common::game::GameVersion::Alicia2
                                              : soa::common::game::GameVersion::Playtest;

        const quint64 scan_generation = generation;
        auto* verification = new QFutureWatcher<QStringList>(this);
        connect(verification, &QFutureWatcher<QStringList>::finished, this,
                [this, verification, context_key, version, root, build, path, scan_generation]()
        {
            QStringList changed = verification->result();
            verification->deleteLater();
            auto context = contexts.find(context_key);
            if (scan_generation != generation || context == contexts.end()
                || context->root != root || context->version != build)
                return;
            context->directory_scan_in_progress = false;
            const bool rescan = context->directory_scan_pending;
            context->directory_scan_pending = false;
            if (suspended)
                return;

            changed.removeDuplicates();
            const QSet<QString> current_changes(changed.cbegin(), changed.cend());
            for (auto reported = context->reported_changes.begin();
                 reported != context->reported_changes.end();)
            {
                if (!current_changes.contains(*reported))
                    reported = context->reported_changes.erase(reported);
                else
                    ++reported;
            }
            if (!changed.isEmpty())
            {
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

            QDirIterator directories(
                root, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                QDirIterator::Subdirectories);
            const QDir rootDir(root);
            while (directories.hasNext())
            {
                const QString directory = QDir::cleanPath(directories.next());
                const QString relative = rootDir.relativeFilePath(directory)
                    .replace(QLatin1Char('\\'), QLatin1Char('/'));
                if (is_internal_launcher_path(relative)
                    || watcher->directories().contains(directory))
                {
                    continue;
                }
                if (watcher->addPath(directory))
                {
                    context->watched_directories.insert(directory);
                    directory_versions.insert(directory, context_key);
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
                if (!info.exists())
                    changed.append(tagged_change("missing", file.key()));
                else if (info.size() != sizes.value(file.key(), -1)
                         || GameIntegrityWatcher::hash_file(
                                absolute, file.value().size()) != file.value())
                    changed.append(tagged_change("modified", file.key()));
            }
            for (const QString& unexpected : unexpected_files(root, hashes))
                changed.append(tagged_change("unexpected", unexpected));
            changed.removeDuplicates();
            return changed;
        }));
    }

    void GameIntegrityWatcher::report_change(const soa::common::game::GameVersion version,
                                             const QStringList& paths)
    {
        auto it = contexts.find(key(version));
        if (it == contexts.end())
            return;

        QStringList fresh;
        for (const QString& path : paths)
        {
            if (path.isEmpty() || it->reported_changes.contains(path))
                continue;
            it->reported_changes.insert(path);
            fresh.append(path);
        }
        if (!fresh.isEmpty())
            emit protected_files_changed(version, fresh);
    }
}
