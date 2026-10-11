#include "runtime/WineRegistry.hpp"

#include <QtGlobal>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSet>
#include <QProcess>
#include <QRegularExpression>
#include <QSysInfo>
#include <QStringList>
#include <QStringView>

#include <functional>
#include <string>

#include "common/AppPaths.hpp"
#include "common/Log.hpp"
#include "runtime/MacWineRuntime.hpp"
#include <spdlog/spdlog.h>

namespace soa::runtime
{
    QString managed_umu_data_home()
    {
#if defined(Q_OS_MACOS)
        return {};
#else
        return QDir(soa::common::paths::application_support_root())
            .filePath(QStringLiteral("runtimes"));
#endif
    }

    QString managed_proton_identifier()
    {
#if defined(Q_OS_MACOS)
        return {};
#else
        return QDir(managed_umu_data_home())
            .filePath(QStringLiteral("umu/compatibilitytools/UMU-Latest"));
#endif
    }

    bool is_managed_proton(const QString& path)
    {
#if defined(Q_OS_MACOS)
        Q_UNUSED(path);
        return false;
#else
        const QString trimmed = path.trimmed();
        if (trimmed.isEmpty())
            return false;
        if (trimmed.compare(QStringLiteral("managed://umu-proton"), Qt::CaseInsensitive) == 0)
            return true;

        const QString candidate = QDir::cleanPath(QFileInfo(trimmed).absoluteFilePath());
        const QString data_home = QDir::cleanPath(
            QFileInfo(managed_umu_data_home()).absoluteFilePath());
        const QString identifier = QDir::cleanPath(
            QFileInfo(managed_proton_identifier()).absoluteFilePath());
        if (candidate == identifier)
            return true;

        const QString name = QFileInfo(candidate).fileName();
        const bool managed_name =
            name.startsWith(QStringLiteral("UMU-Proton"), Qt::CaseInsensitive)
            || name.compare(QStringLiteral("UMU-Latest"), Qt::CaseInsensitive) == 0;
        if (!managed_name)
            return false;

        const QString steam_tools = QDir::cleanPath(QDir(data_home).filePath(
            QStringLiteral("Steam/compatibilitytools.d")));
        const QString umu_tools = QDir::cleanPath(QDir(data_home).filePath(
            QStringLiteral("umu/compatibilitytools")));
        return candidate.startsWith(steam_tools + QDir::separator())
            || candidate.startsWith(umu_tools + QDir::separator());
#endif
    }

    QString resolve_managed_proton_root()
    {
#if defined(Q_OS_MACOS)
        return {};
#else
        const QString data_home = managed_umu_data_home();
        const QString steam_tools = QDir(data_home).filePath(
            QStringLiteral("Steam/compatibilitytools.d"));
        const QString umu_tools = QDir(data_home).filePath(
            QStringLiteral("umu/compatibilitytools"));

        const auto natural_less = [](const QString& left, const QString& right)
        {
            qsizetype i = 0;
            qsizetype j = 0;
            while (i < left.size() && j < right.size())
            {
                if (left[i].isDigit() && right[j].isDigit())
                {
                    const qsizetype left_start = i;
                    const qsizetype right_start = j;
                    while (i < left.size() && left[i].isDigit())
                        ++i;
                    while (j < right.size() && right[j].isDigit())
                        ++j;

                    qsizetype left_significant = left_start;
                    qsizetype right_significant = right_start;
                    while (left_significant < i - 1 && left[left_significant] == QLatin1Char('0'))
                        ++left_significant;
                    while (right_significant < j - 1
                           && right[right_significant] == QLatin1Char('0'))
                    {
                        ++right_significant;
                    }

                    const qsizetype left_digits = i - left_significant;
                    const qsizetype right_digits = j - right_significant;
                    if (left_digits != right_digits)
                        return left_digits < right_digits;

                    const int numeric_compare = QStringView(left).mid(left_significant, left_digits)
                                                    .compare(QStringView(right).mid(
                                                        right_significant, right_digits));
                    if (numeric_compare != 0)
                        return numeric_compare < 0;
                    continue;
                }

                const QChar left_char = left[i].toLower();
                const QChar right_char = right[j].toLower();
                if (left_char != right_char)
                    return left_char.unicode() < right_char.unicode();
                ++i;
                ++j;
            }
            return left.size() < right.size();
        };

        QFileInfo best;
        const auto consider_usable = [&natural_less, &best](
                                         const QString& root,
                                         const std::function<bool(const QString&)>& accepts)
        {
            QDir directory(root);
            if (!directory.exists())
                return;

            for (const QFileInfo& entry : directory.entryInfoList(
                     QDir::Dirs | QDir::NoDotAndDotDot, QDir::NoSort))
            {
                if (!accepts(entry.fileName()))
                    continue;

                const QFileInfo proton(QDir(entry.absoluteFilePath()).filePath(
                    QStringLiteral("proton")));
                if (!proton.isFile() || !proton.isExecutable())
                    continue;
                if (WineRegistry::resolve_wine_executable(entry.absoluteFilePath()).isEmpty())
                    continue;

                if (!best.exists() || natural_less(best.fileName(), entry.fileName()))
                    best = entry;
            }
        };

        const auto is_versioned_umu_proton = [](const QString& name)
        {
            return name.startsWith(QStringLiteral("UMU-Proton"), Qt::CaseInsensitive);
        };
        consider_usable(steam_tools, is_versioned_umu_proton);
        consider_usable(umu_tools, is_versioned_umu_proton);
        if (!best.exists())
        {
            consider_usable(
                umu_tools,
                [](const QString& name)
                {
                    return name.compare(QStringLiteral("UMU-Latest"),
                                        Qt::CaseInsensitive) == 0;
                });
        }

        if (!best.exists())
            return {};
        const QString canonical = best.canonicalFilePath();
        return canonical.isEmpty() ? best.absoluteFilePath() : canonical;
#endif
    }
    QVector<QString>& WineRegistry::extra_search_dirs()
    {
        static QVector<QString> dirs;
        return dirs;
    }

    RuntimeType WineRegistry::identify(const QString& path, bool* ok)
    {
        if (ok) *ok = false;
#if !defined(Q_OS_MACOS)
        if (is_managed_proton(path))
        {
            if (ok) *ok = umu_available();
            return RuntimeType::Proton;
        }
#endif
        const QFileInfo info(path);

        if (info.isFile())
        {
            const bool executable = info.isExecutable();
            const bool proton = info.fileName().compare(QStringLiteral("proton"), Qt::CaseInsensitive) == 0;
#if defined(Q_OS_MACOS)
            if (ok) *ok = executable && !proton;
#else
            if (ok) *ok = executable;
#endif
            return proton ? RuntimeType::Proton : RuntimeType::Wine;
        }

        if (info.isDir())
        {
            const QFileInfo proton(QDir(path).filePath(QStringLiteral("proton")));
            if (proton.isFile())
            {
#if defined(Q_OS_MACOS)
                if (ok) *ok = false;
#else
                if (ok)
                    *ok = proton.isExecutable()
                        && !resolve_wine_executable(info.absoluteFilePath()).isEmpty();
#endif
                return RuntimeType::Proton;
            }

            const QString wine = resolve_wine_executable(path);
            if (!wine.isEmpty())
            {
                if (ok) *ok = QFileInfo(wine).isExecutable();
                return RuntimeType::Wine;
            }
        }

        return RuntimeType::Wine;
    }

    QString WineRegistry::resolve_wine_executable(const QString& path)
    {
#if defined(Q_OS_MACOS)
        return macos::resolve_wine_executable(path);
#else
        const QFileInfo supplied(path);
        if (supplied.isFile() && supplied.isExecutable())
            return supplied.absoluteFilePath();
        if (!supplied.isDir())
            return {};
        const QDir root(supplied.absoluteFilePath());
        for (const QString& relative : {
                 QStringLiteral("files/bin/wine"),
                 QStringLiteral("dist/bin/wine"),
                 QStringLiteral("bin/wine"),
                 QStringLiteral("files/bin/wine64"),
                 QStringLiteral("dist/bin/wine64"),
                 QStringLiteral("bin/wine64")})
        {
            const QFileInfo candidate(root.filePath(relative));
            if (candidate.isFile() && candidate.isExecutable())
                return candidate.absoluteFilePath();
        }
        return {};
#endif
    }

    bool WineRegistry::proton_supports_umu_winetricks(const QString& path)
    {
#if defined(Q_OS_MACOS)
        Q_UNUSED(path);
        return false;
#else
        if (is_managed_proton(path))
            return true;
        if (path.trimmed().isEmpty())
            return false;
        QFileInfo supplied(path);
        const QString root = supplied.isFile() ? supplied.dir().absolutePath()
                                               : supplied.absoluteFilePath();
        if (root.isEmpty())
            return false;

        const QDir runtime(root);
        if (QFileInfo(runtime.filePath(QStringLiteral("protonfixes"))).isDir())
            return true;

        QString identity = QFileInfo(root).fileName();
        QFile version_file(runtime.filePath(QStringLiteral("version")));
        if (version_file.open(QIODevice::ReadOnly | QIODevice::Text))
            identity += QLatin1Char(' ') + QString::fromUtf8(version_file.readLine()).trimmed();

        identity = identity.toLower();
        return identity.contains(QStringLiteral("ge-proton"))
            || identity.contains(QStringLiteral("proton-ge"))
            || identity.contains(QStringLiteral("umu-proton"));
#endif
    }

    bool WineRegistry::inspect_path(const QString& path, WineInstall& install,
                                    QString* error)
    {
        install = {};
        bool identified = false;
        const RuntimeType type = identify(path, &identified);
        if (type == RuntimeType::Proton)
        {
#if defined(Q_OS_MACOS)
            const QString message = QStringLiteral(
                "This runtime type is not supported by the macOS launcher.");
            if (error) *error = message;
            install.issue = message;
            install.type = RuntimeType::Proton;
            install.path = path;
            return false;
#else
            if (!identified)
            {
                const QString message = QStringLiteral("The selected Proton runtime is incomplete or not executable.");
                if (error) *error = message;
                install.issue = message;
                return false;
            }
            install.type = RuntimeType::Proton;
            if (is_managed_proton(path))
            {
                install.path = managed_proton_identifier();
                install.name = QStringLiteral("Install Proton automatically");
                install.version = QStringLiteral("Latest stable UMU-Proton");
                install.architectures = QSysInfo::currentCpuArchitecture();
                install.usable = umu_supports_managed_folders();
                if (!install.usable)
                {
                    install.issue = QStringLiteral(
                        "Automatic Proton setup requires UMU Launcher 1.4.0 or newer.");
                    if (error) *error = install.issue;
                }
                return install.usable;
            }
            install.path = QFileInfo(path).absoluteFilePath();
            install.name = QFileInfo(path).completeBaseName();
            install.usable = true;
            return true;
#endif
        }

#if defined(Q_OS_MACOS)
        const auto probe = macos::probe_runtime(path);
        install.path = path;
        install.type = RuntimeType::Wine;
        install.name = QFileInfo(path).completeBaseName();
        if (install.name.isEmpty())
            install.name = QStringLiteral("Wine");
        install.version = probe.version;
        install.architectures = probe.architectures.join(QLatin1Char(' '));
        if (install.architectures.isEmpty() && QFileInfo(probe.executable).isFile())
            install.architectures = QStringLiteral("script or unknown architecture");
        install.issue = probe.failure;
        install.requires_rosetta = probe.requires_rosetta;
        install.rosetta_available = probe.rosetta_available;
        install.usable = probe.usable;
        if (!probe.usable && error) *error = probe.failure;
        return probe.usable;
#else
        const QString executable = resolve_wine_executable(path);
        if (!identified && executable.isEmpty())
        {
            const QString message = QStringLiteral("No executable Wine entry point was found.");
            if (error) *error = message;
            install.issue = message;
            return false;
        }
        install.path = path;
        install.type = RuntimeType::Wine;
        install.name = QFileInfo(path).completeBaseName();
        install.version = QString();
        install.architectures = QSysInfo::currentCpuArchitecture();
        install.usable = !executable.isEmpty();
        return install.usable;
#endif
    }

    namespace
    {
        QString home() { return QDir::homePath(); }

        QString wine_in(const QString& folder);

        QString runtime_architectures(const QString& executable)
        {
#if defined(Q_OS_MACOS)
            const QStringList architectures = macos::executable_architectures(executable);
            return architectures.isEmpty()
                ? QStringLiteral("script or unknown architecture")
                : architectures.join(QLatin1Char(' '));
#else
            Q_UNUSED(executable);
            return QSysInfo::currentCpuArchitecture();
#endif
        }

        QString probe_runtime_version(const QString& executable)
        {
#if defined(Q_OS_MACOS)
            return macos::probe_runtime(executable).version;
#else
            QProcess process;
            process.setProcessChannelMode(QProcess::MergedChannels);
            process.start(executable, {QStringLiteral("--version")});
            if (!process.waitForStarted(2000))
                return {};
            if (!process.waitForFinished(5000))
            {
                process.terminate();
                if (!process.waitForFinished(1000))
                    process.kill();
                return {};
            }
            if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
                return {};
            return QString::fromUtf8(process.readAll()).trimmed().left(256);
#endif
        }



        QString proton_version(const QString& root, const QString& fallback)
        {
            QFile versionFile(QDir(root).filePath(QStringLiteral("version")));
            if (versionFile.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                const QString version = QString::fromUtf8(versionFile.readLine()).trimmed().left(256);
                if (!version.isEmpty())
                    return version;
            }
            return fallback.left(256);
        }

        bool proton_layout_usable(const QString& root)
        {
            const QFileInfo launcher(QDir(root).filePath(QStringLiteral("proton")));
            if (!launcher.isFile() || !launcher.isExecutable())
                return false;
            for (const QString& relative : {QStringLiteral("files/bin/wine"),
                                            QStringLiteral("files/bin/wine64")})
            {
                const QFileInfo wine(QDir(root).filePath(relative));
                if (wine.isFile() && wine.isExecutable())
                    return true;
            }
            return false;
        }

        void add_install(QVector<WineInstall>& out, QSet<QString>& seen,
                         const QString& path, RuntimeType type, const QString& name)
        {
#if defined(Q_OS_MACOS)
            if (type == RuntimeType::Proton)
                return;
#endif
            const QFileInfo supplied(path);
            QString executable;
            if (type == RuntimeType::Proton)
            {
                executable = supplied.isDir()
                    ? QDir(path).filePath(QStringLiteral("proton"))
                    : supplied.absoluteFilePath();
            }
            else
            {
                executable = WineRegistry::resolve_wine_executable(path);
            }

            const QFileInfo executable_info(executable);
            if (!executable_info.isFile() || !executable_info.isExecutable())
                return;

            const QString canonical = executable_info.canonicalFilePath();
            const QString key = canonical.isEmpty() ? executable_info.absoluteFilePath() : canonical;
            if (seen.contains(key)) return;
            seen.insert(key);

            WineInstall wi;
#if defined(Q_OS_MACOS)
            QString inspectionError;
            (void)WineRegistry::inspect_path(path, wi, &inspectionError);
            if (!name.trimmed().isEmpty())
                wi.name = name;
            if (wi.issue.isEmpty())
                wi.issue = inspectionError;
            if (wi.path.isEmpty())
                wi.path = path;
            out.push_back(wi);
            return;
#else
            wi.name = name;
            wi.path = path;
            wi.type = type;
            if (type == RuntimeType::Proton)
            {
                wi.version = proton_version(path, name);
                const QString wine = wine_in(path);
                wi.architectures = wine.isEmpty() ? QSysInfo::currentCpuArchitecture()
                                                   : runtime_architectures(wine);
                wi.usable = proton_layout_usable(path);
            }
            else
            {
                wi.version = probe_runtime_version(executable);
                wi.architectures = runtime_architectures(executable);
                wi.usable = !wi.version.isEmpty();
            }
            out.push_back(wi);
#endif
        }


        QString runtime_name_for_path(const QString& path)
        {
            const int app_suffix = path.indexOf(QStringLiteral(".app/"), 0, Qt::CaseInsensitive);
            if (app_suffix >= 0)
                return QFileInfo(path.left(app_suffix + 4)).baseName();
            if (path.contains(QStringLiteral("game-porting-toolkit"), Qt::CaseInsensitive))
                return QStringLiteral("Game Porting Toolkit");
            if (path.startsWith(QStringLiteral("/opt/homebrew/"))
                || path.startsWith(QStringLiteral("/usr/local/")))
            {
                return QStringLiteral("Homebrew Wine");
            }
            const QString name = QFileInfo(path).completeBaseName();
            return name.isEmpty() ? QStringLiteral("Wine") : name;
        }

        QString wine_in(const QString& folder)
        {
            for (const QString& rel : { QStringLiteral("/files/bin/wine"),
                                        QStringLiteral("/files/bin/wine64"),
                                        QStringLiteral("/dist/bin/wine"),
                                        QStringLiteral("/dist/bin/wine64"),
                                        QStringLiteral("/bin/wine"),
                                        QStringLiteral("/bin/wine64"),
                                        QStringLiteral("/Contents/Resources/wine/bin/wine"),
                                        QStringLiteral("/Contents/Resources/wine/bin/wine64"),
                                        QStringLiteral("/Contents/SharedSupport/CrossOver/bin/wine"),
                                        QStringLiteral("/Contents/SharedSupport/CrossOver/bin/wine64") })
            {
                const QFileInfo candidate(folder + rel);
                if (candidate.isFile() && candidate.isExecutable()) return candidate.absoluteFilePath();
            }
            return {};
        }

        bool scan_runtime_folder(QVector<WineInstall>& out, QSet<QString>& seen,
                                 const QString& folder, const QString& name)
        {
            if (QFileInfo::exists(folder + QStringLiteral("/proton")))
            {
                add_install(out, seen, folder, RuntimeType::Proton, name);
                return true;
            }
            const QString wine = wine_in(folder);
            if (wine.isEmpty())
                return false;
            add_install(out, seen, folder, RuntimeType::Wine, name);
            return true;
        }

        void scan_runtime_dir(QVector<WineInstall>& out, QSet<QString>& seen, const QString& dir)
        {
            QDir d(dir);
            if (!d.exists()) return;

            for (const QFileInfo& entry : d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
            {
                const QString folder = entry.absoluteFilePath();
                if (scan_runtime_folder(out, seen, folder, entry.completeBaseName()))
                    continue;

                const QDir nested(folder);
                for (const QFileInfo& child : nested.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
                {
                    scan_runtime_folder(
                        out, seen, child.absoluteFilePath(),
                        entry.completeBaseName() + QLatin1Char(' ') + child.completeBaseName());
                }
            }
        }

        QStringList runtime_search_dirs()
        {
            QStringList dirs;

#if defined(Q_OS_MACOS)
            dirs << "/Applications"
                 << home() + "/Applications"
                 << home() + "/Library/Application Support/com.isaacmarovitz.Whisky/Libraries"
                 << "/opt/homebrew/Caskroom"
                 << "/usr/local/Caskroom";
#else
            const QStringList steam_roots
            {
                home() + "/.steam/steam",
                home() + "/.steam/root",
                home() + "/.local/share/Steam",
                home() + "/.var/app/com.valvesoftware.Steam/data/Steam"
            };
            QSet<QString> library_roots;
            for (const QString& root : steam_roots)
            {
                library_roots.insert(QDir::cleanPath(root));
                QFile libraries(QDir(root).filePath(QStringLiteral("steamapps/libraryfolders.vdf")));
                if (libraries.open(QIODevice::ReadOnly | QIODevice::Text))
                {
                    const QString content = QString::fromUtf8(libraries.readAll());
                    const QRegularExpression expression(
                        QStringLiteral(R"soa("path"\s+"([^"]+)")soa"));
                    auto match = expression.globalMatch(content);
                    while (match.hasNext())
                    {
                        QString path = match.next().captured(1);
                        path.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
                        library_roots.insert(QDir::cleanPath(path));
                    }
                }
            }
            for (const QString& root : library_roots)
            {
                dirs << root + "/steamapps/common";
                dirs << root + "/compatibilitytools.d";
            }
            dirs << home() + "/.local/share/lutris/runners/wine"
                 << home() + "/.var/app/net.lutris.Lutris/data/lutris/runners/wine"
                 << home() + "/.config/heroic/tools/wine"
                 << home() + "/.config/heroic/tools/proton"
                 << home() + "/.var/app/com.heroicgameslauncher.hgl/config/heroic/tools/wine"
                 << home() + "/.var/app/com.heroicgameslauncher.hgl/config/heroic/tools/proton"
                 << home() + "/.local/share/bottles/runners"
                 << home() + "/.var/app/com.usebottles.bottles/data/bottles/runners"
                 << "/opt" << "/usr/lib" << "/usr/local";
#endif
            return dirs;
        }

        QStringList direct_binaries()
        {
            QStringList bins;
#if defined(Q_OS_MACOS)
            const QStringList application_roots {QStringLiteral("/Applications"),
                                                  home() + QStringLiteral("/Applications")};
            bins << "/opt/homebrew/bin/wine"
                 << "/opt/homebrew/bin/wine64"
                 << "/usr/local/bin/wine"
                 << "/usr/local/bin/wine64"
                 << "/usr/local/opt/game-porting-toolkit/bin/wine64"
                 << "/opt/homebrew/opt/game-porting-toolkit/bin/wine64";
            for (const QString& root : application_roots)
            {
                bins << root + "/Wine Stable.app/Contents/Resources/wine/bin/wine"
                     << root + "/Wine Stable.app/Contents/Resources/wine/bin/wine64"
                     << root + "/Wine Staging.app/Contents/Resources/wine/bin/wine"
                     << root + "/Wine Staging.app/Contents/Resources/wine/bin/wine64"
                     << root + "/Wine Devel.app/Contents/Resources/wine/bin/wine"
                     << root + "/Wine Devel.app/Contents/Resources/wine/bin/wine64"
                     << root + "/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine"
                     << root + "/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine64"
                     << root + "/Whisky.app/Contents/Resources/Libraries/Wine/bin/wine"
                     << root + "/Whisky.app/Contents/Resources/Libraries/Wine/bin/wine64";
            }
#else
            bins << home() + "/.nix-profile/bin/wine"
                 << home() + "/.nix-profile/bin/wine64"
                 << "/run/current-system/sw/bin/wine"
                 << "/run/current-system/sw/bin/wine64";
#endif
            return bins;
        }
    }

    QVector<WineInstall> WineRegistry::scan()
    {
        QVector<WineInstall> out;
        QSet<QString> seen;

        QString system_wine = QStandardPaths::findExecutable(QStringLiteral("wine"));
        if (system_wine.isEmpty())
            system_wine = QStandardPaths::findExecutable(QStringLiteral("wine64"));
        if (!system_wine.isEmpty())
            add_install(out, seen, system_wine, RuntimeType::Wine, QStringLiteral("System Wine"));

        for (const QString& bin : direct_binaries())
            add_install(out, seen, bin, RuntimeType::Wine, runtime_name_for_path(bin));

        for (const QString& dir : runtime_search_dirs())
        {
#if defined(Q_OS_MACOS)
            scan_runtime_dir(out, seen, dir);
#else
            if (dir == "/opt" || dir.startsWith("/usr"))
            {
                QDir d(dir);
                if (!d.exists()) continue;
                for (const QFileInfo& entry : d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
                {
                    const QString name = entry.fileName();
                    if (!name.contains("wine", Qt::CaseInsensitive) &&
                        !name.contains("proton", Qt::CaseInsensitive))
                        continue;

                    if (const QString folder = entry.absoluteFilePath(); QFileInfo::exists(folder + "/proton"))
                        add_install(out, seen, folder, RuntimeType::Proton, name);
                    else
                    {
                        const QString wine = wine_in(folder);
                        if (!wine.isEmpty())
                            add_install(out, seen, folder, RuntimeType::Wine, name);
                    }
                }
            }
            else
            {
                scan_runtime_dir(out, seen, dir);
            }
#endif
        }

        for (const QString& dir : extra_search_dirs())
            scan_runtime_dir(out, seen, dir);

        SPDLOG_INFO("wine scan: found {} runtime(s)", out.size());
        for (const WineInstall& wi : out)
            SPDLOG_DEBUG("  [{}] {} -> {} ({})",
                         wi.type == RuntimeType::Proton ? "proton" : "wine",
                         wi.name.toStdString(), wi.path.toStdString(),
                         wi.usable ? wi.version.toStdString() : std::string("probe failed"));

        return out;
    }

}
