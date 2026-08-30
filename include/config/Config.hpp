#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include "common/GameVersion.hpp"

class QFileSystemWatcher;
class QTimer;

namespace util::config
{
    class Config : public QObject
    {
        Q_OBJECT

    public:
        static Config& instance();
        ~Config() override;

        QString wine_binary() const;
        QString winetricks_binary() const;
        QString umu_binary() const;
        QString rosetta_x87_path() const;






        QString wine_prefix() const;
        QString proton_compat_data_root() const;
        QString prefix_root() const;
        QString wine_arch() const;
        QString game_install_path() const;
        QString game_install_path(core::game::GameVersion version) const;
        bool    use_dxvk() const;
        bool    runtime_selected() const;
        QString wine_args() const;
        QString macos_compatibility_profile() const;
        bool    diagnostics_enabled() const;

        bool    prerequisites_confirmed() const;
        QString setup_runtime_preference() const;
        bool    rules_accepted() const;
        bool    keep_signed_in() const;

        bool    launch_on_startup() const;
        QString after_game_start() const;
        QString launcher_size() const;
        QString language() const;

        core::game::GameVersion game_version() const;
        QString game_id() const;
        QString game_args() const;

        QString username() const;
        QString token() const;
        QString display_name() const;
        bool    has_auth() const;
        bool    game_installed() const;
        bool    path_inside_prefix(const QString& path) const;

        void set_wine_binary(const QString& value);
        void set_winetricks_binary(const QString& value);
        void set_umu_binary(const QString& value);
        void set_rosetta_x87_path(const QString& value);
        void set_wine_prefix(const QString& value);
        void set_game_install_path(const QString& value);
        void forget_game_install_path();
        void set_wine_arch(const QString& value);
        void set_use_dxvk(bool value);
        void set_runtime_selected(bool value);
        void set_wine_args(const QString& value);
        void set_macos_compatibility_profile(const QString& value);
        void set_diagnostics_enabled(bool value);

        void set_prerequisites_confirmed(bool value);
        void set_setup_runtime_preference(const QString& value);
        void set_rules_accepted(bool value);
        void set_keep_signed_in(bool value);

        void set_launch_on_startup(bool value);
        void set_after_game_start(const QString& value);
        void set_launcher_size(const QString& value);
        void set_language(const QString& value);

        void set_game_version(core::game::GameVersion value);
        void set_game_args(const QString& value);

        void set_auth(const QString& username, const QString& token,
                      const QString& display_name = {});
        void clear_auth();
        bool reset_launcher_config();

        QString file_path() const;
        QString env_path() const;
        QString persistence_error() const;

        void reload();


        void begin_update();
        void end_update();

    signals:
        void changed();
        void persistence_failed(const QString& path, const QString& reason);

    private:
        explicit Config(QObject* parent = nullptr);
        Config(const Config&) = delete;
        Config& operator=(const Config&) = delete;

        enum class LoadOutcome
        {
            Loaded,
            Missing,
            Unreadable
        };



        LoadOutcome load_document();
        bool load();
        bool save();
        bool restore_from_backup();
        bool write_backup(const QByteArray& contents) const;
        QString backup_path() const;
        void load_credentials();
        bool save_credentials();
        bool clear_saved_credentials();
        bool load_env_fallback();
        bool save_env_fallback();
        void apply_defaults();
        void probe_system_paths();
        void watch_files();
        void schedule_reload();
        void reload_from_disk(bool force = false);
        void remember_disk_state();
        [[nodiscard]] bool disk_state_changed() const;

        QString derive_game_path(const QString& prefix,
                                 core::game::GameVersion version) const;
        QString normalize_game_path(const QString& path) const;
        QString normalize_wine_prefix(const QString& path) const;
        QString normalize_proton_compat_root(const QString& path) const;
        void rebase_game_install_paths(const QString& old_prefix,
                                       const QString& old_playtest_path,
                                       const QString& old_alicia2_path);
        bool runtime_is_proton() const;
        void persist_change();
        void normalize_schema();
        static QString game_install_path_key(core::game::GameVersion version);

        class Impl;
        Impl* d {};

        QFileSystemWatcher* watcher {};
        QTimer* reload_timer {};
        QTimer* integrity_timer {};
        int consecutive_unreadable {};
        bool recovering {};
        QByteArray config_digest;
        QByteArray env_digest;
        bool writing {};
        bool reloading {};
        int update_depth {};
        bool update_dirty {};
    };
}
