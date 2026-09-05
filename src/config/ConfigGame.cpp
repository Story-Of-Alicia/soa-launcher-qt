#include "ConfigPrivate.hpp"

namespace util::config
{
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

}
