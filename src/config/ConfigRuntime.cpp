#include "ConfigPrivate.hpp"

namespace util::config
{
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

    bool Config::path_inside_prefix(const QString& path) const
    {
        const QString candidate = normalize_game_path(path);
        if (candidate.isEmpty())
            return false;

        const QString rootAbsolute = absolute_clean_path(prefix_root());
        return core::wine::host_path_is_inside_prefix(rootAbsolute, candidate);
    }

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

}
