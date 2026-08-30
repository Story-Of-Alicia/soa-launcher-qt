#include "config/Config.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QRegularExpression>
#include <QTimer>
#include <QVariantMap>

#include <utility>

#include "common/Log.hpp"
#include "runtime/WineProcess.hpp"
#include "runtime/WineRegistry.hpp"
#include "runtime/MacWineRuntime.hpp"
#include "credentials/CredentialStore.hpp"
#include <spdlog/spdlog.h>

namespace util::config
{
    namespace
    {
        QString absolute_clean_path(const QString& path)
        {
            return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
        }

        bool path_has_prefix(const QString& candidate, const QString& root)
        {
            return candidate == root || candidate.startsWith(root + QDir::separator());
        }

        QString host_wine_user()
        {
            return qEnvironmentVariable(
                "USER", QDir::homePath().section(QLatin1Char('/'), -1));
        }

        QString game_path_for_user(const QString& prefix,
                                   const core::game::GameVersion version,
                                   const QString& user)
        {
            const QString folder = QString::fromLatin1(
                core::game::profile(version).default_install_directory);
            return QDir(prefix).filePath(
                QStringLiteral("drive_c/users/%1/AppData/Roaming/%2/game")
                    .arg(user, folder));
        }



        constexpr int k_integrity_interval_ms = 5000;
        constexpr int k_integrity_max_ms = 300000;

        QByteArray file_digest(const QString& path)
        {
            const QFileInfo info(path);
            if (!info.exists())
                return QByteArrayLiteral("<missing>");

            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                return QByteArrayLiteral("<unreadable>");
            return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
        }

    }

        QString normalized_launcher_size(const QString& value)
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


        QString normalized_language(const QString& value)
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

        QString normalized_after_launch(const QString& value)
        {
            const QString candidate = value.trimmed().toLower();
            return candidate == QStringLiteral("minimize")
                ? candidate : QStringLiteral("keep");
        }

        QString bounded_arguments(const QString& value)
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

    Config& Config::instance()
    {
        static Config instance;
        return instance;
    }

    Config::Config(QObject* parent) : QObject(parent), d(new Impl)
    {
        const QString directory = QFileInfo(file_path()).absolutePath();
        if (!QDir().mkpath(directory))
            SPDLOG_ERROR("config: could not create config directory {}", directory.toStdString());

        (void)load();
        apply_defaults();
        normalize_schema();
        if (keep_signed_in())
        {
            load_credentials();
        }
        else
        {
            d->username.clear();
            d->token.clear();
            d->display_name.clear();
            if (!clear_saved_credentials())
                SPDLOG_WARN("config: could not fully clear non-persistent credentials at startup");
        }
        save();

        watcher = new QFileSystemWatcher(this);
        reload_timer = new QTimer(this);
        reload_timer->setSingleShot(true);
        reload_timer->setInterval(150);
        connect(reload_timer, &QTimer::timeout, this, [this]() { reload_from_disk(); });
        connect(watcher, &QFileSystemWatcher::fileChanged, this,
                [this](const QString&) { schedule_reload(); });
        connect(watcher, &QFileSystemWatcher::directoryChanged, this,
                [this](const QString&) { schedule_reload(); });





        integrity_timer = new QTimer(this);
        integrity_timer->setInterval(k_integrity_interval_ms);
        connect(integrity_timer, &QTimer::timeout, this, [this]()
        {
            if (writing || reloading)
                return;
            if (QFileInfo::exists(file_path()))
            {
                integrity_timer->setInterval(k_integrity_interval_ms);
                return;
            }
            reload_from_disk(true);
            if (!persistence_error().isEmpty())
            {
                const int next =
                    qMin(integrity_timer->interval() * 4, k_integrity_max_ms);
                integrity_timer->setInterval(next);
                SPDLOG_WARN("config: recovery write failed, retrying in {} s", next / 1000);
            }
        });
        integrity_timer->start();

        watch_files();
        remember_disk_state();
    }

    Config::~Config()
    {
        delete d;
    }

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

    void Config::normalize_schema()
    {
#if defined(Q_OS_MACOS)
        d->values[QStringLiteral("wine_arch")] = QStringLiteral("win64");
#else
        const QString arch = d->values.value(QStringLiteral("wine_arch")).toString().toLower();
        d->values[QStringLiteral("wine_arch")] = arch == QStringLiteral("win32")
            ? QStringLiteral("win32") : QStringLiteral("win64");
#endif
        d->values[QStringLiteral("after_game_start")] = normalized_after_launch(
            d->values.value(QStringLiteral("after_game_start")).toString());
        d->values[QStringLiteral("launcher_size")] = normalized_launcher_size(
            d->values.value(QStringLiteral("launcher_size")).toString());
        d->values[QStringLiteral("language")] = normalized_language(
            d->values.value(QStringLiteral("language")).toString());
        d->values[QStringLiteral("wine_args")] = bounded_arguments(
            d->values.value(QStringLiteral("wine_args")).toString());
        d->values[QStringLiteral("game_args")] = bounded_arguments(
            d->values.value(QStringLiteral("game_args")).toString());
        d->values[QStringLiteral("umu_binary")] =
            d->values.value(QStringLiteral("umu_binary")).toString().trimmed();
        d->values[QStringLiteral("use_dxvk")] =
            d->values.value(QStringLiteral("use_dxvk")).toBool();
        d->values[QStringLiteral("runtime_selected")] =
            d->values.value(QStringLiteral("runtime_selected")).toBool();
        d->values[QStringLiteral("diagnostics_enabled")] =
            d->values.value(QStringLiteral("diagnostics_enabled")).toBool();
        d->values[QStringLiteral("prerequisites_confirmed")] =
            d->values.value(QStringLiteral("prerequisites_confirmed")).toBool();
        d->values[QStringLiteral("rules_accepted")] =
            d->values.value(QStringLiteral("rules_accepted")).toBool();
        d->values[QStringLiteral("keep_signed_in")] =
            d->values.value(QStringLiteral("keep_signed_in")).toBool();
        d->values[QStringLiteral("launch_on_startup")] =
            d->values.value(QStringLiteral("launch_on_startup")).toBool();
        d->values[QStringLiteral("setup_assistant_version")] =
            qMax(0, d->values.value(QStringLiteral("setup_assistant_version")).toInt());

        const QString profile =
            d->values.value(QStringLiteral("macos_compatibility_profile"))
                .toString().trimmed().toLower();
        d->values[QStringLiteral("macos_compatibility_profile")] =
            profile == QStringLiteral("safe-display")
                || profile == QStringLiteral("low-graphics")
                || profile == QStringLiteral("gl-behind")
            ? profile : QStringLiteral("default");

        d->values[QStringLiteral("game_version")] =
            core::game::to_string(core::game::game_version_from_string(
                d->values.value(QStringLiteral("game_version")).toString()));
        const QString preference = d->values.value(QStringLiteral("setup_runtime_preference")).toString().toLower();
#if defined(Q_OS_MACOS)
        d->values[QStringLiteral("setup_runtime_preference")] = QStringLiteral("wine");
#else
        d->values[QStringLiteral("setup_runtime_preference")] =
            preference == QStringLiteral("wine") || preference == QStringLiteral("proton")
                ? preference : QStringLiteral("recommended");
#endif
    }

    bool Config::load_env_fallback()
    {
        QFile file(env_path());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return false;

        const QByteArray contents = file.readAll();
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(contents, &error);
        if (error.error == QJsonParseError::NoError && document.isObject())
        {
            const QJsonObject object = document.object();
            d->username = object.value(QStringLiteral("user")).toString();
            d->token = object.value(QStringLiteral("token")).toString();
            d->display_name = object.value(QStringLiteral("display_name")).toString();
            return has_auth();
        }


        const QString text = QString::fromUtf8(contents);
        for (const QString& rawLine : text.split(QLatin1Char('\n')))
        {
            const QString line = rawLine.trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;
            const int equals = line.indexOf(QLatin1Char('='));
            if (equals <= 0)
                continue;

            const QString key = line.left(equals).trimmed();
            QString value = line.mid(equals + 1).trimmed();
            if (value.size() >= 2 && value.startsWith(QLatin1Char('"'))
                && value.endsWith(QLatin1Char('"')))
            {
                value = value.mid(1, value.size() - 2);
            }

            if (key == QStringLiteral("SOA_USER")) d->username = value;
            else if (key == QStringLiteral("SOA_TOKEN")) d->token = value;
            else if (key == QStringLiteral("SOA_USERNAME")) d->display_name = value;
        }
        return has_auth();
    }

    void Config::load_credentials()
    {
        d->username.clear();
        d->token.clear();
        d->display_name.clear();

        util::credentials::Credentials credentials;
        if (util::credentials::CredentialStore::load(credentials))
        {
            d->username = credentials.user;
            d->token = credentials.token;
            d->display_name = credentials.display_name;
            SPDLOG_DEBUG("config: loaded credentials from platform credential store");
            return;
        }

        if (load_env_fallback())
            SPDLOG_WARN("config: using protected .env credential fallback");
        else
            SPDLOG_DEBUG("config: no saved credentials");
    }

    bool Config::save_env_fallback()
    {
        if (!has_auth())
            return !QFileInfo::exists(env_path()) || QFile::remove(env_path());

        const QJsonObject object {
            {QStringLiteral("user"), d->username},
            {QStringLiteral("token"), d->token},
            {QStringLiteral("display_name"), d->display_name}
        };
        const QByteArray contents = QJsonDocument(object).toJson(QJsonDocument::Compact);

        QSaveFile file(env_path());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        if (file.write(contents) != contents.size() || !file.commit())
            return false;
        if (!QFile::setPermissions(env_path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        {
            SPDLOG_ERROR("config: could not restrict .env permissions");
            QFile::remove(env_path());
            remember_disk_state();
            return false;
        }
        remember_disk_state();
        return true;
    }

    bool Config::clear_saved_credentials()
    {
        const bool store_cleared = !util::credentials::CredentialStore::available()
            || util::credentials::CredentialStore::clear();
        const bool fallback_cleared = !QFileInfo::exists(env_path()) || QFile::remove(env_path());
        watch_files();
        remember_disk_state();
        return store_cleared && fallback_cleared;
    }

    bool Config::save_credentials()
    {
        if (!has_auth())
        {
            return clear_saved_credentials();
        }

        const util::credentials::Credentials credentials {
            d->username, d->token, d->display_name
        };
        if (util::credentials::CredentialStore::save(credentials))
        {
            QFile::remove(env_path());
            watch_files();
            remember_disk_state();
            return true;
        }

        SPDLOG_WARN("config: platform credential store unavailable; using protected .env fallback");
        const bool ok = save_env_fallback();
        watch_files();
        return ok;
    }

    QString Config::game_install_path_key(const core::game::GameVersion version)
    {
        return version == core::game::GameVersion::Alicia2
            ? QStringLiteral("game_install_path_2_0")
            : QStringLiteral("game_install_path_1_0");
    }

    QString Config::derive_game_path(const QString& prefix,
                                     const core::game::GameVersion version) const
    {
        const bool proton = core::wine::WineRegistry::identify(wine_binary())
            == core::wine::RuntimeType::Proton;
        return game_path_for_user(prefix, version,
                                  proton ? QStringLiteral("steamuser")
                                         : host_wine_user());
    }

    void Config::probe_system_paths()
    {
        QStringList extraDirectories;
#ifdef Q_OS_MACOS
        extraDirectories << QStringLiteral("/opt/homebrew/bin") << QStringLiteral("/usr/local/bin");
#endif
        auto probe = [this, &extraDirectories](const QString& key, const QString& executable)
        {
            if (!d->values.value(key).toString().isEmpty())
                return;
            QString found = QStandardPaths::findExecutable(executable);
            if (found.isEmpty() && !extraDirectories.isEmpty())
                found = QStandardPaths::findExecutable(executable, extraDirectories);
            if (!found.isEmpty())
                d->values[key] = found;
        };

        probe(QStringLiteral("wine_binary"), QStringLiteral("wine"));
        if (d->values.value(QStringLiteral("wine_binary")).toString().isEmpty())
            probe(QStringLiteral("wine_binary"), QStringLiteral("wine64"));
        probe(QStringLiteral("winetricks_binary"), QStringLiteral("winetricks"));
#if !defined(Q_OS_MACOS)
        probe(QStringLiteral("umu_binary"), QStringLiteral("umu-run"));
        if (d->values.value(QStringLiteral("umu_binary")).toString().isEmpty())
        {
            const QString local = QDir::home().filePath(QStringLiteral(".local/bin/umu-run"));
            if (QFileInfo(local).isExecutable())
                d->values[QStringLiteral("umu_binary")] = local;
        }
#endif
    }

    void Config::apply_defaults()
    {
        probe_system_paths();
        auto setIfMissing = [this](const QString& key, const QVariant& value)
        {
            if (!d->values.contains(key))
                d->values[key] = value;
        };

#if defined(Q_OS_MACOS)
        const QString defaultPrefix = core::wine::macos::default_prefix_root();
#else
        const QString defaultPrefix = QDir(QDir::homePath()).filePath(QStringLiteral("soa-launcher"));
#endif
        const bool hadProtonRoot = d->values.contains(QStringLiteral("proton_compat_data_root"));
        const QString legacyPrefix = d->values.value(QStringLiteral("wine_prefix"), defaultPrefix).toString();

        if (!hadProtonRoot && runtime_is_proton())
        {


            d->values[QStringLiteral("proton_compat_data_root")] =
                normalize_proton_compat_root(legacyPrefix);
            d->values[QStringLiteral("wine_prefix")] = defaultPrefix;
        }
        else
        {
            setIfMissing(QStringLiteral("wine_prefix"), defaultPrefix);
            setIfMissing(QStringLiteral("proton_compat_data_root"), defaultPrefix);
        }

        setIfMissing(QStringLiteral("wine_arch"), QStringLiteral("win64"));
        setIfMissing(QStringLiteral("wine_binary"), QString());
        setIfMissing(QStringLiteral("winetricks_binary"), QString());
        setIfMissing(QStringLiteral("umu_binary"), QString());
        setIfMissing(QStringLiteral("use_dxvk"), false);
        setIfMissing(QStringLiteral("runtime_selected"), false);
        setIfMissing(QStringLiteral("wine_args"), QString());
        setIfMissing(QStringLiteral("macos_compatibility_profile"), QStringLiteral("default"));
        if (!d->values.contains(QStringLiteral("diagnostics_enabled")) &&
            d->values.contains(QStringLiteral("macos_deep_diagnostics")))
        {
            d->values[QStringLiteral("diagnostics_enabled")] =
                d->values.value(QStringLiteral("macos_deep_diagnostics")).toBool();
        }
        setIfMissing(QStringLiteral("diagnostics_enabled"), false);
        d->values.remove(QStringLiteral("macos_deep_diagnostics"));
        setIfMissing(QStringLiteral("rosetta_x87_path"), QString());
        setIfMissing(QStringLiteral("prerequisites_confirmed"), false);
        setIfMissing(QStringLiteral("setup_assistant_version"), 0);
        setIfMissing(QStringLiteral("setup_runtime_preference"), QStringLiteral("recommended"));
        setIfMissing(QStringLiteral("rules_accepted"), false);
        setIfMissing(QStringLiteral("keep_signed_in"), false);
        setIfMissing(QStringLiteral("launch_on_startup"), false);
        setIfMissing(QStringLiteral("after_game_start"), QStringLiteral("keep"));
        setIfMissing(QStringLiteral("launcher_size"), QStringLiteral("1400x846"));
        setIfMissing(QStringLiteral("language"), QStringLiteral("en"));
        setIfMissing(QStringLiteral("game_version"), QStringLiteral("1.0"));
        setIfMissing(QStringLiteral("game_args"), QString());

        const QString legacyPath = d->values.value(QStringLiteral("game_install_path")).toString();
        setIfMissing(QStringLiteral("game_install_path_1_0"), legacyPath);
        setIfMissing(QStringLiteral("game_install_path_2_0"), QString());
        d->values.remove(QStringLiteral("game_install_path"));
        d->values.remove(QStringLiteral("game_id"));
        d->values.remove(QStringLiteral("setup_pc_age"));

#if defined(Q_OS_MACOS)
        const QString oldDefault = QDir(QDir::homePath()).filePath(QStringLiteral("soa-launcher"));
        const QString storedPrefix = absolute_clean_path(
            d->values.value(QStringLiteral("wine_prefix")).toString());
        if (storedPrefix == absolute_clean_path(oldDefault) && !QDir(oldDefault).exists())
            d->values[QStringLiteral("wine_prefix")] = defaultPrefix;
        d->values[QStringLiteral("wine_arch")] = QStringLiteral("win64");
        d->values[QStringLiteral("use_dxvk")] = false;
        if (d->values.value(QStringLiteral("setup_runtime_preference")).toString() == QStringLiteral("proton"))
            d->values[QStringLiteral("setup_runtime_preference")] = QStringLiteral("wine");
        if (core::wine::WineRegistry::identify(
                d->values.value(QStringLiteral("wine_binary")).toString())
            == core::wine::RuntimeType::Proton)
        {
            SPDLOG_INFO("config: clearing Linux Proton selection on macOS");
            d->values[QStringLiteral("wine_binary")] = QString();
            d->values[QStringLiteral("runtime_selected")] = false;
        }
        if (d->values.value(QStringLiteral("wine_binary")).toString().trimmed()
                == QStringLiteral("managed://active"))
        {
            SPDLOG_INFO("config: clearing obsolete managed Wine selection on macOS");
            d->values[QStringLiteral("wine_binary")] = QString();
            d->values[QStringLiteral("runtime_selected")] = false;
        }
#endif
        d->values[QStringLiteral("wine_prefix")] = normalize_wine_prefix(
            d->values.value(QStringLiteral("wine_prefix")).toString());
        d->values[QStringLiteral("proton_compat_data_root")] = normalize_proton_compat_root(
            d->values.value(QStringLiteral("proton_compat_data_root")).toString());

        const QString activePrefix = absolute_clean_path(prefix_root());
        const bool proton = runtime_is_proton();
        for (const auto version : {core::game::GameVersion::Playtest,
                                   core::game::GameVersion::Alicia2})
        {
            const QString key = game_install_path_key(version);
            const QString stored = d->values.value(key).toString().trimmed();
            QString normalized = stored.isEmpty()
                ? derive_game_path(activePrefix, version)
                : normalize_game_path(stored);
            const QString fallback = derive_game_path(activePrefix, version);

            if (proton && host_wine_user() != QStringLiteral("steamuser"))
            {
                const QString preselectionDefault = absolute_clean_path(
                    game_path_for_user(activePrefix, version, host_wine_user()));
                if (absolute_clean_path(normalized) == preselectionDefault)
                {
                    SPDLOG_INFO("config: migrated pre-Proton game path {} to {}",
                                normalized.toStdString(), fallback.toStdString());
                    normalized = fallback;
                }
            }

            if (path_has_prefix(normalized, activePrefix))
            {
                d->values[key] = normalized;
                continue;
            }

            SPDLOG_WARN("config: replacing game path {} outside active prefix {} with {}",
                        normalized.toStdString(), activePrefix.toStdString(),
                        fallback.toStdString());
            d->values[key] = fallback;
        }
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

    QString Config::wine_binary() const { return d->values.value(QStringLiteral("wine_binary")).toString(); }
    QString Config::winetricks_binary() const { return d->values.value(QStringLiteral("winetricks_binary")).toString(); }
    QString Config::umu_binary() const { return d->values.value(QStringLiteral("umu_binary")).toString(); }
    QString Config::rosetta_x87_path() const { return d->values.value(QStringLiteral("rosetta_x87_path")).toString(); }
    QString Config::wine_arch() const
    {
        const QString value = d->values.value(QStringLiteral("wine_arch")).toString();
        return value.isEmpty() ? QStringLiteral("win64") : value;
    }
    bool Config::use_dxvk() const { return d->values.value(QStringLiteral("use_dxvk")).toBool(); }
    bool Config::runtime_selected() const { return d->values.value(QStringLiteral("runtime_selected")).toBool(); }
    QString Config::wine_args() const { return d->values.value(QStringLiteral("wine_args")).toString(); }
    QString Config::macos_compatibility_profile() const
    {
        const QString value = d->values.value(QStringLiteral("macos_compatibility_profile"))
                                  .toString().trimmed().toLower();
        return value == QStringLiteral("safe-display")
            || value == QStringLiteral("low-graphics")
            || value == QStringLiteral("gl-behind")
            || value == QStringLiteral("audio-isolation")
            ? value : QStringLiteral("default");
    }
    bool Config::diagnostics_enabled() const
    {
        return d->values.value(QStringLiteral("diagnostics_enabled")).toBool();
    }
    bool Config::prerequisites_confirmed() const
    {
        return d->values.value(QStringLiteral("prerequisites_confirmed")).toBool()
            && d->values.value(QStringLiteral("setup_assistant_version")).toInt() >= 1;
    }
    QString Config::setup_runtime_preference() const
    {
        const QString value = d->values.value(QStringLiteral("setup_runtime_preference")).toString().toLower();
#if defined(Q_OS_MACOS)
        return value == QStringLiteral("wine") ? value : QStringLiteral("recommended");
#else
        return value == QStringLiteral("wine") || value == QStringLiteral("proton")
            ? value : QStringLiteral("recommended");
#endif
    }
    bool Config::rules_accepted() const { return d->values.value(QStringLiteral("rules_accepted")).toBool(); }
    bool Config::keep_signed_in() const { return d->values.value(QStringLiteral("keep_signed_in")).toBool(); }
    bool Config::launch_on_startup() const { return d->values.value(QStringLiteral("launch_on_startup")).toBool(); }
    QString Config::after_game_start() const
    {
        const QString value = d->values.value(QStringLiteral("after_game_start")).toString();
        return value.isEmpty() ? QStringLiteral("keep") : value;
    }
    QString Config::launcher_size() const
    {
        const QString value = d->values.value(QStringLiteral("launcher_size")).toString();
        return value.isEmpty() ? QStringLiteral("1400x846") : value;
    }
    QString Config::language() const
    {
        return normalized_language(d->values.value(QStringLiteral("language")).toString());
    }

    core::game::GameVersion Config::game_version() const
    {
        return core::game::game_version_from_string(
            d->values.value(QStringLiteral("game_version")).toString());
    }

    QString Config::game_id() const
    {
        return QString::fromLatin1(core::game::profile(game_version()).launch_game_id);
    }

    QString Config::game_args() const
    {
        return d->values.value(QStringLiteral("game_args")).toString();
    }

    QString Config::persistence_error() const
    {
        return d->persistence_error;
    }

    bool Config::runtime_is_proton() const
    {
#if defined(Q_OS_MACOS)
        return false;
#else
        return core::wine::WineRegistry::identify(wine_binary())
            == core::wine::RuntimeType::Proton;
#endif
    }

    QString Config::normalize_wine_prefix(const QString& path) const
    {
        QString configured = path.trimmed();
        if (configured.isEmpty())
        {
#if defined(Q_OS_MACOS)
            configured = core::wine::macos::default_prefix_root();
#else
            configured = QDir(QDir::homePath()).filePath(QStringLiteral("soa-launcher"));
#endif
        }
        return absolute_clean_path(configured);
    }

    QString Config::normalize_proton_compat_root(const QString& path) const
    {
        QString configured = normalize_wine_prefix(path);
        if (QFileInfo(configured).fileName().compare(QStringLiteral("pfx"), Qt::CaseInsensitive) == 0)
        {
            const QString parent = QFileInfo(configured).dir().absolutePath();
            SPDLOG_INFO("config: normalized Proton pfx selection {} to compat-data root {}",
                        configured.toStdString(), parent.toStdString());
            configured = QDir::cleanPath(parent);
        }
        return configured;
    }

    QString Config::wine_prefix() const
    {
        return runtime_is_proton()
            ? proton_compat_data_root()
            : normalize_wine_prefix(d->values.value(QStringLiteral("wine_prefix")).toString());
    }

    QString Config::proton_compat_data_root() const
    {
        return normalize_proton_compat_root(
            d->values.value(QStringLiteral("proton_compat_data_root")).toString());
    }

    QString Config::prefix_root() const
    {
        return runtime_is_proton()
            ? QDir(proton_compat_data_root()).filePath(QStringLiteral("pfx"))
            : normalize_wine_prefix(d->values.value(QStringLiteral("wine_prefix")).toString());
    }

    QString Config::normalize_game_path(const QString& path) const
    {
        if (path.trimmed().isEmpty())
            return {};

        const QString candidate = absolute_clean_path(path);
        const QString prefix = absolute_clean_path(prefix_root());
        if (path_has_prefix(candidate, prefix))
            return candidate;

        const QString compat = absolute_clean_path(proton_compat_data_root());
        if (compat != prefix && path_has_prefix(candidate, compat))
        {
            const QString relative = QDir(compat).relativeFilePath(candidate);
            return relative == QStringLiteral(".")
                ? prefix
                : QDir::cleanPath(QDir(prefix).filePath(relative));
        }
        return candidate;
    }

    QString Config::game_install_path() const
    {
        return game_install_path(game_version());
    }

    QString Config::game_install_path(const core::game::GameVersion version) const
    {
        const QString stored = d->values.value(game_install_path_key(version)).toString();
        return normalize_game_path(stored.isEmpty() ? derive_game_path(prefix_root(), version) : stored);
    }

    bool Config::path_inside_prefix(const QString& path) const
    {
        const QString candidate = normalize_game_path(path);
        if (candidate.isEmpty())
            return false;

        const QString rootAbsolute = absolute_clean_path(prefix_root());
        return core::wine::host_path_is_inside_prefix(rootAbsolute, candidate);
    }

    QString Config::username() const { return d->username; }
    QString Config::token() const { return d->token; }
    QString Config::display_name() const { return d->display_name; }
    bool Config::has_auth() const { return !d->username.isEmpty() && !d->token.isEmpty(); }

    void Config::set_wine_binary(const QString& value)
    {
        if (wine_binary() == value)
            return;

        const bool oldProton = runtime_is_proton();
        const QString oldPrefix = prefix_root();
        const QString oldPlaytestPath = game_install_path(core::game::GameVersion::Playtest);
        const QString oldAlicia2Path = game_install_path(core::game::GameVersion::Alicia2);
        const QString oldPlaytestDefault = derive_game_path(
            oldPrefix, core::game::GameVersion::Playtest);
        const QString oldAlicia2Default = derive_game_path(
            oldPrefix, core::game::GameVersion::Alicia2);

        d->values[QStringLiteral("wine_binary")] = value;
        rebase_game_install_paths(oldPrefix, oldPlaytestPath, oldAlicia2Path);

        if (oldProton != runtime_is_proton())
        {
            const auto updateDefault = [this](const core::game::GameVersion version,
                                              const QString& oldPath,
                                              const QString& oldDefault)
            {
                if (absolute_clean_path(oldPath) != absolute_clean_path(oldDefault))
                    return;
                d->values[game_install_path_key(version)] =
                    derive_game_path(prefix_root(), version);
            };
            updateDefault(core::game::GameVersion::Playtest,
                          oldPlaytestPath, oldPlaytestDefault);
            updateDefault(core::game::GameVersion::Alicia2,
                          oldAlicia2Path, oldAlicia2Default);
        }
        persist_change();
    }
    void Config::set_winetricks_binary(const QString& value)
    {
        if (winetricks_binary() == value) return;
        d->values[QStringLiteral("winetricks_binary")] = value; persist_change();
    }
    void Config::set_umu_binary(const QString& value)
    {
        const QString normalized = value.trimmed();
        if (umu_binary() == normalized) return;
        d->values[QStringLiteral("umu_binary")] = normalized; persist_change();
    }
    void Config::set_rosetta_x87_path(const QString& value)
    {
        if (rosetta_x87_path() == value) return;
        d->values[QStringLiteral("rosetta_x87_path")] = value; persist_change();
    }
    void Config::set_wine_arch(const QString& value)
    {
#if defined(Q_OS_MACOS)
        Q_UNUSED(value);
        const QString normalized = QStringLiteral("win64");
#else
        const QString normalized = value.trimmed().toLower() == QStringLiteral("win32") ? QStringLiteral("win32") : QStringLiteral("win64");
#endif
        if (wine_arch() == normalized) return;
        d->values[QStringLiteral("wine_arch")] = normalized; persist_change();
    }
    void Config::set_use_dxvk(const bool value)
    {
#if defined(Q_OS_MACOS)
        const bool normalized = false;
        Q_UNUSED(value);
#else
        const bool normalized = value;
#endif
        if (use_dxvk() == normalized) return;
        d->values[QStringLiteral("use_dxvk")] = normalized; persist_change();
    }
    void Config::set_runtime_selected(const bool value)
    {
        if (runtime_selected() == value) return;
        d->values[QStringLiteral("runtime_selected")] = value; persist_change();
    }
    void Config::set_wine_args(const QString& value)
    {
        const QString normalized = bounded_arguments(value);
        if (wine_args() == normalized) return;
        d->values[QStringLiteral("wine_args")] = normalized; persist_change();
    }
    void Config::set_macos_compatibility_profile(const QString& value)
    {
        const QString candidate = value.trimmed().toLower();
        const QString normalized = candidate == QStringLiteral("safe-display")
            || candidate == QStringLiteral("low-graphics")
            || candidate == QStringLiteral("gl-behind")
            || candidate == QStringLiteral("audio-isolation")
            ? candidate : QStringLiteral("default");
        if (macos_compatibility_profile() == normalized) return;
        d->values[QStringLiteral("macos_compatibility_profile")] = normalized;
        persist_change();
    }
    void Config::set_diagnostics_enabled(const bool value)
    {
        if (diagnostics_enabled() == value)
            return;
        d->values[QStringLiteral("diagnostics_enabled")] = value;
        persist_change();
    }
    void Config::set_prerequisites_confirmed(const bool value)
    {
        const bool assistantAlreadyCurrent = d->values.value(QStringLiteral("setup_assistant_version")).toInt() == 1;
        if (prerequisites_confirmed() == value && (!value || assistantAlreadyCurrent))
            return;
        d->values[QStringLiteral("prerequisites_confirmed")] = value;
        if (value)
            d->values[QStringLiteral("setup_assistant_version")] = 1;
        persist_change();
    }
    void Config::set_setup_runtime_preference(const QString& value)
    {
#if defined(Q_OS_MACOS)
        const QString normalized = value == QStringLiteral("wine")
            ? value : QStringLiteral("recommended");
#else
        const QString normalized = value == QStringLiteral("wine") || value == QStringLiteral("proton")
            ? value : QStringLiteral("recommended");
#endif
        if (setup_runtime_preference() == normalized)
            return;
        d->values[QStringLiteral("setup_runtime_preference")] = normalized;
        persist_change();
    }
    void Config::set_rules_accepted(const bool value)
    {
        if (rules_accepted() == value) return;
        d->values[QStringLiteral("rules_accepted")] = value; persist_change();
    }
    void Config::set_keep_signed_in(const bool value)
    {
        if (keep_signed_in() == value)
            return;

        d->values[QStringLiteral("keep_signed_in")] = value;
        if (update_depth > 0) update_dirty = true;
        else save();

        if (value)
        {
            if (has_auth() && !save_credentials())
                SPDLOG_ERROR("config: failed to persist credentials after enabling keep-signed-in");
        }
        else if (!clear_saved_credentials())
        {
            SPDLOG_WARN("config: could not fully remove saved credentials after disabling keep-signed-in");
        }

        if (update_depth == 0) emit changed();
    }
    void Config::set_launch_on_startup(const bool value)
    {
        if (launch_on_startup() == value) return;
        d->values[QStringLiteral("launch_on_startup")] = value; persist_change();
    }
    void Config::set_after_game_start(const QString& value)
    {
        const QString normalized = normalized_after_launch(value);
        if (after_game_start() == normalized) return;
        d->values[QStringLiteral("after_game_start")] = normalized; persist_change();
    }
    void Config::set_launcher_size(const QString& value)
    {
        const QString normalized = normalized_launcher_size(value);
        if (launcher_size() == normalized) return;
        d->values[QStringLiteral("launcher_size")] = normalized; persist_change();
    }
    void Config::set_language(const QString& value)
    {
        const QString normalized = normalized_language(value);
        if (language() == normalized) return;
        d->values[QStringLiteral("language")] = normalized; persist_change();
    }
    void Config::set_game_version(const core::game::GameVersion value)
    {
        if (game_version() == value) return;
        d->values[QStringLiteral("game_version")] = core::game::to_string(value); persist_change();
    }
    void Config::set_game_args(const QString& value)
    {
        const QString normalized = bounded_arguments(value);
        if (game_args() == normalized) return;
        d->values[QStringLiteral("game_args")] = normalized; persist_change();
    }

    void Config::set_wine_prefix(const QString& value)
    {
        const bool proton = runtime_is_proton();
        const QString normalized = proton
            ? normalize_proton_compat_root(value)
            : normalize_wine_prefix(value);
        if (wine_prefix() == normalized)
            return;

        const QString oldPrefix = prefix_root();
        const QString oldPlaytestPath = game_install_path(core::game::GameVersion::Playtest);
        const QString oldAlicia2Path = game_install_path(core::game::GameVersion::Alicia2);

        if (proton)
            d->values[QStringLiteral("proton_compat_data_root")] = normalized;
        else
            d->values[QStringLiteral("wine_prefix")] = normalized;

        rebase_game_install_paths(oldPrefix, oldPlaytestPath, oldAlicia2Path);
        persist_change();
    }

    void Config::rebase_game_install_paths(const QString& old_prefix,
                                           const QString& old_playtest_path,
                                           const QString& old_alicia2_path)
    {
        const QString oldPrefix = absolute_clean_path(old_prefix);
        const QString newPrefix = absolute_clean_path(prefix_root());
        if (oldPrefix == newPrefix)
            return;

        const auto rebase = [this, &oldPrefix, &newPrefix](
            const core::game::GameVersion version, const QString& oldPath)
        {
            const QString key = game_install_path_key(version);
            const QString candidate = absolute_clean_path(oldPath);
            if (!path_has_prefix(candidate, oldPrefix))
            {
                SPDLOG_WARN("config: resetting stale game path {} after prefix changed to {}",
                            candidate.toStdString(), newPrefix.toStdString());
                d->values[key] = derive_game_path(newPrefix, version);
                return;
            }

            const QString relative = QDir(oldPrefix).relativeFilePath(candidate);
            const QString rebased = relative == QStringLiteral(".")
                ? newPrefix
                : QDir::cleanPath(QDir(newPrefix).filePath(relative));
            d->values[key] = rebased;
            SPDLOG_INFO("config: rebased game path {} to {}",
                        candidate.toStdString(), rebased.toStdString());
        };

        rebase(core::game::GameVersion::Playtest, old_playtest_path);
        rebase(core::game::GameVersion::Alicia2, old_alicia2_path);
    }

    void Config::set_game_install_path(const QString& value)
    {
        const QString normalized = value.trimmed().isEmpty()
            ? derive_game_path(prefix_root(), game_version())
            : normalize_game_path(value);
        if (game_install_path() == normalized)
            return;
        d->values[game_install_path_key(game_version())] = normalized;
        persist_change();
    }

    void Config::forget_game_install_path()
    {
        const QString key = game_install_path_key(game_version());
        const QString defaultPath = derive_game_path(prefix_root(), game_version());
        if (d->values.value(key).toString() == defaultPath)
            return;
        d->values[key] = defaultPath;
        persist_change();
    }

    bool Config::game_installed() const
    {
        const QDir directory(game_install_path());
        if (!directory.exists())
            return false;
        const auto& game = core::game::profile(game_version());
        const QFileInfo versionFile(directory.filePath(QString::fromLatin1(game.install_marker_file)));
        const QFileInfo executable(directory.filePath(QString::fromLatin1(game.executable_name)));
        return versionFile.isFile() && versionFile.size() > 0 && executable.isFile();
    }

    void Config::set_auth(const QString& username, const QString& token,
                          const QString& displayName)
    {
        d->username = username;
        d->token = token;
        d->display_name = displayName;
        if (keep_signed_in())
        {
            if (!save_credentials())
                SPDLOG_ERROR("config: failed to persist credentials securely");
        }
        else if (!clear_saved_credentials())
        {
            SPDLOG_WARN("config: could not clear old saved credentials for this session-only login");
        }
        emit changed();
    }

    void Config::clear_auth()
    {
        d->username.clear();
        d->token.clear();
        d->display_name.clear();
        if (!clear_saved_credentials())
            SPDLOG_WARN("config: could not fully clear the platform credential store");
        emit changed();
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
