#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QtGlobal>

#include "common/GameVersion.hpp"

class QFileSystemWatcher;
class QTimer;

namespace core::network
{
    class SwiftHttpClient;
}

namespace core::integrity
{
    class GameIntegrityWatcher : public QObject
    {
        Q_OBJECT

    public:
        explicit GameIntegrityWatcher(QObject* parent = nullptr);

        void refresh();
        void set_suspended(bool suspended);

    signals:
        void protected_files_changed(core::game::GameVersion version, const QStringList& paths);

    private:
        struct Context
        {
            QString root;
            QString version;
            QHash<QString, QByteArray> hashes;
            QHash<QString, qint64> sizes;
            QSet<QString> watched_files;
            QSet<QString> watched_directories;
            bool ready {};
            bool alerted {};
            bool directory_scan_in_progress {};
            bool directory_scan_pending {};
        };

        void clear_watchers();
        void load_context(core::game::GameVersion version);
        void fetch_manifest(core::game::GameVersion version, const QString& root, const QString& build);
        void apply_manifest(core::game::GameVersion version, const QByteArray& payload);
        void install_watchers(core::game::GameVersion version);
        void inspect_file(const QString& path);
        void inspect_directory(const QString& path);
        void report_change(core::game::GameVersion version, const QStringList& paths);
        void reset_after_suspension();
        static int key(core::game::GameVersion version);
        static QString safe_relative_path(const QString& value);
        static QByteArray hash_file(const QString& path, qsizetype expected_hex_length);

        QFileSystemWatcher* watcher {};
        core::network::SwiftHttpClient* network {};
        QTimer* refresh_timer {};
        QHash<int, Context> contexts;
        QHash<QString, int> file_versions;
        QHash<QString, int> directory_versions;
        QSet<QString> reported_watch_failures;
        QSet<QString> reported_restore_failures;
        bool suspended {};
        bool pending_refresh {};
    };
}
