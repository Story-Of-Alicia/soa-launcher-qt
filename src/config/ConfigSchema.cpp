#include "ConfigPrivate.hpp"

namespace util::config
{
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

}
