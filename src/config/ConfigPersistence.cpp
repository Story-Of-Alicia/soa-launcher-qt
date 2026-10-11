#include "ConfigPrivate.hpp"
#include "common/AppPaths.hpp"

#if defined(Q_OS_MACOS)
#include <QProcess>
#include <unistd.h>
#endif

namespace soa::config
{
    QString Config::file_path() const
    {
#if defined(Q_OS_MACOS)
        return QDir(soa::runtime::macos::application_support_root())
            .filePath(QStringLiteral("state/config.json"));
#else
        return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
            .filePath(QStringLiteral("config.json"));
#endif
    }

    QString Config::env_path() const
    {
#if defined(Q_OS_MACOS)
        return QDir(soa::runtime::macos::application_support_root())
            .filePath(QStringLiteral("state/.env"));
#else
        return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
            .filePath(QStringLiteral(".env"));
#endif
    }

    QString Config::recovery_marker_path() const
    {
        QString root = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        if (root.isEmpty())
            root = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
        if (root.isEmpty())
            root = QDir::homePath();
        return QDir(root).filePath(QStringLiteral(".recreate-config"));
    }

    bool Config::recovery_marker_exists() const
    {
        return QFileInfo(recovery_marker_path()).isFile();
    }

    bool Config::write_recovery_marker() const
    {
        const QString marker = recovery_marker_path();
        const QString directory = QFileInfo(marker).absolutePath();
        if (!QFileInfo(directory).isDir() && !QDir().mkpath(directory))
        {
            SPDLOG_WARN("config: could not create recovery marker directory {}",
                        directory.toStdString());
            return false;
        }

        QSaveFile file(marker);
        if (!file.open(QIODevice::WriteOnly)
            || file.write(QByteArrayLiteral("ready\n")) < 0 || !file.commit())
        {
            SPDLOG_WARN("config: could not create setup recovery marker {}",
                        marker.toStdString());
            return false;
        }
        return true;
    }

    void Config::watch_files()
    {
        if (!watcher)
            return;

        const QString directory = QFileInfo(file_path()).absolutePath();
        if (QFileInfo(directory).isDir() && !watcher->directories().contains(directory))
            watcher->addPath(directory);

        const QStringList desired {file_path(), env_path()};
        for (const QString& path : desired)
        {
            if (QFileInfo::exists(path) && !watcher->files().contains(path))
                watcher->addPath(path);
        }
    }

    QString Config::backup_path() const
    {
        return file_path() + QStringLiteral(".bak");
    }

    Config::LoadOutcome Config::load_document()
    {
        const QString path = file_path();
        if (!QFileInfo::exists(path))
        {
            SPDLOG_DEBUG("config: no existing file at {}", path.toStdString());
            return LoadOutcome::Missing;
        }

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        {
            SPDLOG_ERROR("config: could not open {} for reading", path.toStdString());
            return LoadOutcome::Unreadable;
        }

        const QByteArray raw = file.readAll();
        file.close();




        if (raw.isEmpty())
        {
            SPDLOG_WARN("config: {} is empty", path.toStdString());
            return LoadOutcome::Unreadable;
        }

        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(raw, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
        {
            SPDLOG_ERROR("config: failed to parse config.json: {}", error.errorString().toStdString());
            return LoadOutcome::Unreadable;
        }

        d->values = document.object().toVariantMap();
        return LoadOutcome::Loaded;
    }

    bool Config::restore_from_backup()
    {
        const QString path = backup_path();
        if (!QFileInfo::exists(path))
            return false;

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return false;

        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
        {
            SPDLOG_WARN("config: backup at {} is unusable", path.toStdString());
            return false;
        }

        d->values = document.object().toVariantMap();
        return true;
    }

    bool Config::write_backup(const QByteArray& contents) const
    {
        QSaveFile backup(backup_path());
        if (!backup.open(QIODevice::WriteOnly))
            return false;
        return backup.write(contents) >= 0 && backup.commit();
    }

    bool Config::load()
    {
        switch (load_document())
        {
        case LoadOutcome::Loaded:
            return true;

        case LoadOutcome::Missing:
            if (recovery_marker_exists() && restore_from_backup())
            {
                SPDLOG_WARN("config: config.json was missing at startup; recovered from {}",
                            backup_path().toStdString());
                return true;
            }
            d->values.clear();
            return true;

        case LoadOutcome::Unreadable:
            if (recovery_marker_exists() && restore_from_backup())
            {
                SPDLOG_WARN("config: config.json was unreadable at startup; recovered from {}",
                            backup_path().toStdString());
                return true;
            }
            d->values.clear();
            return false;
        }
        return false;
    }

    bool Config::save()
    {
        const QJsonDocument document(QJsonObject::fromVariantMap(d->values));
        const QByteArray payload = document.toJson(QJsonDocument::Indented);
        writing = true;
        if (watcher)
        {
            watcher->removePath(file_path());
            watcher->removePath(env_path());
        }




        const QString directory = QFileInfo(file_path()).absolutePath();
        if (!QFileInfo(directory).isDir() && !QDir().mkpath(directory))
            SPDLOG_ERROR("config: could not recreate config directory {}", directory.toStdString());

        QSaveFile file(file_path());
        bool ok = file.open(QIODevice::WriteOnly);
        if (ok)
        {
            ok = file.write(payload) >= 0 && file.commit();
        }
        if (!ok)
        {
            d->persistence_error = file.errorString();
            SPDLOG_ERROR("config: failed to save {}", file_path().toStdString());




            if (!recovering)
                emit persistence_failed(file_path(), d->persistence_error);
        }
        else
        {
            d->persistence_error.clear();
            if (!write_backup(payload))
                SPDLOG_DEBUG("config: could not refresh {}", backup_path().toStdString());
        }

        writing = false;
        watch_files();
        remember_disk_state();
        return ok;
    }

    void Config::begin_update()
    {
        ++update_depth;
    }

    void Config::end_update()
    {
        if (update_depth <= 0)
        {
            update_depth = 0;
            return;
        }
        --update_depth;
        if (update_depth == 0 && update_dirty)
        {
            update_dirty = false;
            save();
            emit changed();
        }
    }

    void Config::persist_change()
    {
        if (update_depth > 0)
        {
            update_dirty = true;
            return;
        }
        save();
        emit changed();
    }

    void Config::schedule_reload()
    {
        if (writing || reloading || !reload_timer)
            return;
        reload_timer->start();
    }

    void Config::remember_disk_state()
    {
        config_digest = file_digest(file_path());
        env_digest = file_digest(env_path());
    }

    bool Config::disk_state_changed() const
    {
        return config_digest != file_digest(file_path())
            || env_digest != file_digest(env_path());
    }

    void Config::reload_from_disk(const bool force)
    {
        if (writing || reloading)
            return;

        watch_files();
        if (!force && !disk_state_changed())
            return;

        reloading = true;
        const QVariantMap previousValues = d->values;
        const bool previouslyKeptSignedIn = keep_signed_in();
        const QString sessionUser = d->username;
        const QString sessionToken = d->token;
        const QString sessionDisplayName = d->display_name;
        const bool hadSessionCredentials = !sessionUser.isEmpty() && !sessionToken.isEmpty();

        SPDLOG_INFO("config: files changed externally, reloading");




        constexpr int k_unreadable_tolerance = 3;

        const LoadOutcome outcome = load_document();
        bool recovered = false;

        if (outcome == LoadOutcome::Unreadable)
        {
            d->values = previousValues;
            ++consecutive_unreadable;
            if (consecutive_unreadable < k_unreadable_tolerance)
            {



                reloading = false;
                watch_files();
                SPDLOG_WARN("config: config.json unreadable, retrying ({}/{})",
                            consecutive_unreadable, k_unreadable_tolerance);
                if (reload_timer)
                    reload_timer->start();
                return;
            }
            if (!recovery_marker_exists())
            {
                consecutive_unreadable = 0;
                reloading = false;
                remember_disk_state();
                SPDLOG_WARN("config: config.json stayed unreadable while automatic recovery is disabled");
                return;
            }
            SPDLOG_ERROR("config: config.json stayed unreadable; rewriting it from the "
                         "running configuration");
            recovered = true;
        }
        else if (outcome == LoadOutcome::Missing)
        {
            d->values = previousValues;
            if (!recovery_marker_exists())
            {
                consecutive_unreadable = 0;
                reloading = false;
                remember_disk_state();
                SPDLOG_INFO("config: config.json is absent while automatic recovery is disabled");
                return;
            }
            SPDLOG_WARN("config: config.json disappeared while running; rewriting it from the "
                        "running configuration");
            recovered = true;
        }

        consecutive_unreadable = 0;
        apply_defaults();
        normalize_schema();

        if (keep_signed_in())
        {
            if (!previouslyKeptSignedIn && hadSessionCredentials)
            {

                d->username = sessionUser;
                d->token = sessionToken;
                d->display_name = sessionDisplayName;
                if (!save_credentials())
                    SPDLOG_WARN("config: could not persist active session after enabling keep-signed-in externally");
            }
            else
            {
                load_credentials();
                if (!has_auth() && hadSessionCredentials)
                {
                    d->username = sessionUser;
                    d->token = sessionToken;
                    d->display_name = sessionDisplayName;
                }
            }
        }
        else
        {

            d->username = sessionUser;
            d->token = sessionToken;
            d->display_name = sessionDisplayName;



            if ((previouslyKeptSignedIn || QFileInfo::exists(env_path()))
                && !clear_saved_credentials())
            {
                SPDLOG_WARN("config: could not fully clear credentials after keep-signed-in was disabled");
            }
        }

        const bool valuesChanged = previousValues != d->values;
        const bool credentialsChanged = sessionUser != d->username
            || sessionToken != d->token
            || sessionDisplayName != d->display_name;

        if (recovered || valuesChanged)
        {
            recovering = recovered;
            (void)save();
            recovering = false;
        }
        else
        {
            watch_files();
            remember_disk_state();
        }
        reloading = false;
        if (valuesChanged || credentialsChanged)
            emit changed();
        else if (recovered)
            SPDLOG_INFO("config: file restored with no change to the running configuration");
        else
            SPDLOG_DEBUG("config: external rewrite contained no effective changes");
    }

    void Config::reload()
    {
        reload_from_disk(true);
    }

    QString Config::persistence_error() const
    {
        return d->persistence_error;
    }

    bool Config::mark_setup_complete()
    {
        if (!language_selected() || !prerequisites_confirmed()
            || !runtime_selected() || !rules_accepted() || !QFileInfo(file_path()).isFile())
        {
            return false;
        }

        if (recovery_marker_exists())
            return true;

        if (!write_recovery_marker())
            return false;

        SPDLOG_DEBUG("config: setup recovery marker created");
        return true;
    }

    bool Config::reset_launcher_config()
    {
        const QString marker = recovery_marker_path();
        const bool had_recovery_marker = QFileInfo::exists(marker);
        if (had_recovery_marker && !QFile::remove(marker))
        {
            SPDLOG_ERROR("config: could not remove setup recovery marker {}", marker.toStdString());
            return false;
        }

        writing = true;
        reloading = false;
        recovering = false;
        update_depth = 0;
        update_dirty = false;
        consecutive_unreadable = 0;

        if (reload_timer)
            reload_timer->stop();
        if (integrity_timer)
            integrity_timer->stop();
        if (watcher)
        {
            const QStringList files = watcher->files();
            if (!files.isEmpty())
                watcher->removePaths(files);
            const QStringList directories = watcher->directories();
            if (!directories.isEmpty())
                watcher->removePaths(directories);
        }

        if (QFileInfo::exists(file_path()) && !QFile::remove(file_path()))
        {
            writing = false;
            if (had_recovery_marker && !write_recovery_marker())
                SPDLOG_ERROR("config: could not restore setup recovery marker after reset failure");
            watch_files();
            if (integrity_timer)
            {
                integrity_timer->setInterval(k_integrity_interval_ms);
                integrity_timer->start();
            }
            SPDLOG_ERROR("config: factory reset could not remove {}", file_path().toStdString());
            return false;
        }

        d->username.clear();
        d->token.clear();
        d->display_name.clear();

        const bool credential_store_cleared = !soa::credentials::CredentialStore::available()
            || soa::credentials::CredentialStore::clear();
        const bool fallback_cleared = !QFileInfo::exists(env_path()) || QFile::remove(env_path());

        bool startup_entry_removed = true;
#if defined(Q_OS_LINUX)
        const QString config_root =
            QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
        if (!config_root.isEmpty())
        {
            const QString startup_path = QDir(config_root).filePath(
                QStringLiteral("autostart/soa-launcher.desktop"));
            startup_entry_removed = !QFileInfo::exists(startup_path)
                || QFile::remove(startup_path);
        }
#elif defined(Q_OS_MACOS)
        const QString startup_path = QDir::home().filePath(
            QStringLiteral("Library/LaunchAgents/com.storyofalicia.launcher.plist"));
        if (QFileInfo::exists(startup_path))
        {
            const QString domain = QStringLiteral("gui/%1").arg(static_cast<qulonglong>(getuid()));
            (void)QProcess::execute(QStringLiteral("/bin/launchctl"),
                                    {QStringLiteral("bootout"), domain, startup_path});
            startup_entry_removed = QFile::remove(startup_path);
        }
#endif

        const QString data_root = soa::common::paths::application_support_root();
        bool data_removed = true;
        if (!data_root.isEmpty() && QDir(data_root).exists())
            data_removed = QDir(data_root).removeRecursively();

        const QString qt_data_root =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        bool qt_data_removed = true;
        if (!qt_data_root.isEmpty()
            && QDir::cleanPath(qt_data_root) != QDir::cleanPath(data_root)
            && QDir(qt_data_root).exists())
        {
            qt_data_removed = QDir(qt_data_root).removeRecursively();
        }

        const QString cache_root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        bool cache_removed = true;
        if (!cache_root.isEmpty()
            && QDir::cleanPath(cache_root) != QDir::cleanPath(data_root)
            && QDir(cache_root).exists())
        {
            cache_removed = QDir(cache_root).removeRecursively();
        }

        d->values.clear();
        d->persistence_error.clear();
        apply_defaults();
        normalize_schema();
        config_digest = QByteArrayLiteral("<missing>");
        env_digest = QByteArrayLiteral("<missing>");

        writing = false;
        if (integrity_timer)
        {
            integrity_timer->setInterval(k_integrity_interval_ms);
            integrity_timer->start();
        }

        emit changed();

        if (!credential_store_cleared)
            SPDLOG_ERROR("config: factory reset could not fully clear saved credentials");
        if (!fallback_cleared)
            SPDLOG_ERROR("config: factory reset could not remove the credential fallback");
        if (!startup_entry_removed)
            SPDLOG_ERROR("config: factory reset could not remove the launch-on-startup entry");
        if (!data_removed)
            SPDLOG_ERROR("config: factory reset could not fully remove {}", data_root.toStdString());
        if (!qt_data_removed)
            SPDLOG_ERROR("config: factory reset could not fully remove {}", qt_data_root.toStdString());
        if (!cache_removed)
            SPDLOG_ERROR("config: factory reset could not fully remove {}", cache_root.toStdString());

        return credential_store_cleared && fallback_cleared && startup_entry_removed
            && data_removed && qt_data_removed && cache_removed;
    }

}
