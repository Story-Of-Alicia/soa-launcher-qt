#include "ConfigPrivate.hpp"

namespace util::config
{
    QString Config::file_path() const
    {
#if defined(Q_OS_MACOS)
        return QDir(core::wine::macos::application_support_root())
            .filePath(QStringLiteral("state/config.json"));
#else
        return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
            .filePath(QStringLiteral("config.json"));
#endif
    }

    QString Config::env_path() const
    {
#if defined(Q_OS_MACOS)
        return QDir(core::wine::macos::application_support_root())
            .filePath(QStringLiteral("state/.env"));
#else
        return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
            .filePath(QStringLiteral(".env"));
#endif
    }

    void Config::watch_files()
    {
        if (!watcher)
            return;

        const QString directory = QFileInfo(file_path()).absolutePath();


        if (!QFileInfo(directory).isDir())
            QDir().mkpath(directory);
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
            if (restore_from_backup())
            {
                SPDLOG_WARN("config: config.json was missing at startup; recovered from {}",
                            backup_path().toStdString());
                return true;
            }
            d->values.clear();
            return true;

        case LoadOutcome::Unreadable:
            if (restore_from_backup())
            {
                SPDLOG_WARN("config: config.json was unreadable at startup; recovered from {}",
                            backup_path().toStdString());
                return true;
            }
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
            SPDLOG_ERROR("config: config.json stayed unreadable; rewriting it from the "
                         "running configuration");
            recovered = true;
        }
        else if (outcome == LoadOutcome::Missing)
        {




            d->values = previousValues;
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

    bool Config::reset_launcher_config()
    {
        writing = true;
        if (watcher)
        {
            watcher->removePath(file_path());
            watcher->removePath(env_path());
        }

        const bool configRemoved = !QFileInfo::exists(file_path()) || QFile::remove(file_path());


        if (QFileInfo::exists(backup_path()))
            (void)QFile::remove(backup_path());
        d->values.clear();
        d->username.clear();
        d->token.clear();
        d->display_name.clear();
        const bool credentialsCleared = save_credentials();
        apply_defaults();
        normalize_schema();
        writing = false;
        const bool saved = save();
        watch_files();
        emit changed();
        return configRemoved && credentialsCleared && saved;
    }

}
