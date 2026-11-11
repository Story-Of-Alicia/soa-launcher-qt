// Story of Alicia Launcher
// SPDX-FileCopyrightText: 2026 Story of Alicia contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional terms apply, see ADDITIONAL_TERMS.md.

#include "ConfigPrivate.hpp"
#include "common/AppPaths.hpp"

namespace soa::config
{
    QString Config::wine_binary() const { return d->values.value(QStringLiteral("wine_binary")).toString(); }

    QString Config::winetricks_binary() const { return d->values.value(QStringLiteral("winetricks_binary")).toString(); }

    QString Config::umu_binary() const { return d->values.value(QStringLiteral("umu_binary")).toString(); }

    QString Config::wine_arch() const
    {
        const QString value = d->values.value(QStringLiteral("wine_arch")).toString();
        return value.isEmpty() ? QStringLiteral("win64") : value;
    }

    bool Config::use_dxvk() const { return d->values.value(QStringLiteral("use_dxvk")).toBool(); }

    bool Config::runtime_selected() const { return d->values.value(QStringLiteral("runtime_selected")).toBool(); }

    QString Config::wine_args() const { return d->values.value(QStringLiteral("wine_args")).toString(); }

    bool Config::diagnostics_enabled() const
    {
        return d->values.value(QStringLiteral("diagnostics_enabled")).toBool();
    }

    QString Config::setup_runtime_preference() const
    {
        const QString value = d->values.value(QStringLiteral("setup_runtime_preference")).toString().toLower();
        return value == QStringLiteral("wine") || value == QStringLiteral("proton")
            ? value : QStringLiteral("proton");
    }

    bool Config::runtime_is_proton() const
    {
        return soa::runtime::WineRegistry::identify(wine_binary())
            == soa::runtime::RuntimeType::Proton;
    }

    QString Config::normalize_wine_prefix(const QString& path) const
    {
        QString configured = path.trimmed();
        if (configured.isEmpty())
        {
            configured = soa::common::paths::default_prefix_root();
        }
        return absolute_clean_path(configured);
    }

    QString Config::normalize_proton_compat_root(const QString& path) const
    {
        QString configured = path.trimmed();
        if (configured.isEmpty())
        {
            configured = soa::common::paths::default_proton_compat_data_root();
        }
        configured = absolute_clean_path(configured);
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
        return soa::runtime::host_path_is_inside_prefix(rootAbsolute, candidate);
    }

    void Config::set_wine_binary(const QString& value)
    {
        QString normalized = value.trimmed();
        if (normalized.compare(QStringLiteral("managed://umu-proton"), Qt::CaseInsensitive) == 0)
            normalized = soa::runtime::managed_proton_identifier();
        if (!normalized.isEmpty() && !QFileInfo(normalized).isAbsolute())
        {
            const QString found = QStandardPaths::findExecutable(normalized);
            if (!found.isEmpty())
                normalized = found;
        }
        if (QFileInfo(normalized).isAbsolute())
            normalized = QDir::cleanPath(QFileInfo(normalized).absoluteFilePath());
        if (wine_binary() == normalized)
            return;

        const bool oldProton = runtime_is_proton();
        const QString oldPrefix = prefix_root();
        const QString oldPlaytestPath = game_install_path(soa::common::game::GameVersion::Playtest);
        const QString oldAlicia2Path = game_install_path(soa::common::game::GameVersion::Alicia2);
        const QString oldPlaytestDefault = derive_game_path(
            oldPrefix, soa::common::game::GameVersion::Playtest);
        const QString oldAlicia2Default = derive_game_path(
            oldPrefix, soa::common::game::GameVersion::Alicia2);

        d->values[QStringLiteral("wine_binary")] = normalized;
        rebase_game_install_paths(oldPrefix, oldPlaytestPath, oldAlicia2Path);

        if (oldProton != runtime_is_proton())
        {
            const auto updateDefault = [this](const soa::common::game::GameVersion version,
                                              const QString& oldPath,
                                              const QString& oldDefault)
            {
                if (absolute_clean_path(oldPath) != absolute_clean_path(oldDefault))
                    return;
                d->values[game_install_path_key(version)] =
                    derive_game_path(prefix_root(), version);
            };
            updateDefault(soa::common::game::GameVersion::Playtest,
                          oldPlaytestPath, oldPlaytestDefault);
            updateDefault(soa::common::game::GameVersion::Alicia2,
                          oldAlicia2Path, oldAlicia2Default);
        }
        persist_change();
    }

    void Config::set_winetricks_binary(const QString& value)
    {
        QString normalized = value.trimmed();
        if (!normalized.isEmpty() && !QFileInfo(normalized).isAbsolute())
        {
            const QString found = QStandardPaths::findExecutable(normalized);
            if (!found.isEmpty())
                normalized = found;
        }
        if (QFileInfo(normalized).isAbsolute())
            normalized = QDir::cleanPath(QFileInfo(normalized).absoluteFilePath());
        if (winetricks_binary() == normalized) return;
        d->values[QStringLiteral("winetricks_binary")] = normalized; persist_change();
    }

    void Config::set_umu_binary(const QString& value)
    {
        QString normalized = value.trimmed();
        if (!normalized.isEmpty() && !QFileInfo(normalized).isAbsolute())
        {
            const QString found = QStandardPaths::findExecutable(normalized);
            if (!found.isEmpty())
                normalized = found;
        }
        if (QFileInfo(normalized).isAbsolute())
            normalized = QDir::cleanPath(QFileInfo(normalized).absoluteFilePath());
        if (umu_binary() == normalized) return;
        d->values[QStringLiteral("umu_binary")] = normalized; persist_change();
    }

    void Config::set_wine_arch(const QString& value)
    {
        const QString normalized = value.trimmed().toLower() == QStringLiteral("win32") ? QStringLiteral("win32") : QStringLiteral("win64");
        if (wine_arch() == normalized) return;
        d->values[QStringLiteral("wine_arch")] = normalized; persist_change();
    }

    void Config::set_use_dxvk(const bool value)
    {
        const bool normalized = value;
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

    void Config::set_diagnostics_enabled(const bool value)
    {
        if (diagnostics_enabled() == value)
            return;
        d->values[QStringLiteral("diagnostics_enabled")] = value;
        persist_change();
    }

    void Config::set_setup_runtime_preference(const QString& value)
    {
        const QString normalized = value == QStringLiteral("wine") || value == QStringLiteral("proton")
            ? value : QStringLiteral("proton");
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
        const QString oldPlaytestPath = game_install_path(soa::common::game::GameVersion::Playtest);
        const QString oldAlicia2Path = game_install_path(soa::common::game::GameVersion::Alicia2);

        if (proton)
            d->values[QStringLiteral("proton_compat_data_root")] = normalized;
        else
            d->values[QStringLiteral("wine_prefix")] = normalized;

        rebase_game_install_paths(oldPrefix, oldPlaytestPath, oldAlicia2Path);
        persist_change();
    }

}
