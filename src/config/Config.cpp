#include "ConfigPrivate.hpp"

namespace util::config
{
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

}
