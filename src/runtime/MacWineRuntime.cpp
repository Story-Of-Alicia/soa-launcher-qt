#include "runtime/MacWineRuntime.hpp"
#include "common/AppPaths.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QSysInfo>

namespace core::wine::macos
{
    namespace
    {
        QStringList candidate_relative_paths()
        {
            return {
                QStringLiteral("bin/wine"),
                QStringLiteral("files/bin/wine"),
                QStringLiteral("dist/bin/wine"),
                QStringLiteral("Contents/Resources/wine/bin/wine"),
                QStringLiteral("Contents/Resources/Libraries/Wine/bin/wine"),
                QStringLiteral("Contents/SharedSupport/CrossOver/bin/wine"),
                QStringLiteral("bin/wine64"),
                QStringLiteral("files/bin/wine64"),
                QStringLiteral("dist/bin/wine64"),
                QStringLiteral("Contents/Resources/wine/bin/wine64"),
                QStringLiteral("Contents/Resources/Libraries/Wine/bin/wine64"),
                QStringLiteral("Contents/SharedSupport/CrossOver/bin/wine64")
            };
        }

        bool is_apple_silicon_host()
        {
#if defined(Q_OS_MACOS)
            const QString architecture = QSysInfo::currentCpuArchitecture().toLower();
            return architecture == QStringLiteral("arm64")
                || architecture == QStringLiteral("arm64e")
                || architecture == QStringLiteral("aarch64");
#else
            return false;
#endif
        }

        bool is_script(const QString& executable)
        {
            QFile file(executable);
            if (!file.open(QIODevice::ReadOnly))
                return false;
            return file.read(2) == QByteArrayLiteral("#!");
        }

        QString run_version_probe(const QString& executable, const int timeout_ms,
                                  QString* failure)
        {
            QStringList arguments {QStringLiteral("--version")};
            const QString program = prepare_host_launch(executable, arguments);
            if (program.isEmpty())
            {
                if (failure)
                    *failure = QStringLiteral(
                        "The Wine host command could not be prepared.");
                return {};
            }

            QProcess process;
            QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
            apply_runtime_environment(environment, executable);
            process.setProcessEnvironment(environment);
            process.setProcessChannelMode(QProcess::MergedChannels);
            process.start(program, arguments);
            if (!process.waitForStarted(qMin(timeout_ms, 2500)))
            {
                if (failure)
                {
                    *failure = QStringLiteral("Wine failed to start: %1")
                        .arg(process.errorString());
                }
                return {};
            }
            if (!process.waitForFinished(timeout_ms))
            {
                process.terminate();
                if (!process.waitForFinished(1000))
                    process.kill();
                process.waitForFinished(1000);
                if (failure)
                    *failure = QStringLiteral(
                        "Wine did not answer its version request before the timeout.");
                return {};
            }
            if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
            {
                if (failure)
                {
                    const QString output = QString::fromUtf8(process.readAll()).trimmed().left(512);
                    *failure = output.isEmpty()
                        ? QStringLiteral("The Wine version request exited with code %1.")
                              .arg(process.exitCode())
                        : QStringLiteral("The Wine version request exited with code %1: %2")
                              .arg(process.exitCode()).arg(output);
                }
                return {};
            }
            return QString::fromUtf8(process.readAll()).trimmed().left(256);
        }

        void prepend_path(QProcessEnvironment& environment,
                          const QString& key,
                          const QStringList& values)
        {
            QStringList existing = environment.value(key).split(QLatin1Char(':'), Qt::SkipEmptyParts);
            QStringList merged;
            for (const QString& value : values)
            {
                const QString clean = QDir::cleanPath(value);
                if (!clean.isEmpty() && QDir(clean).exists() && !merged.contains(clean))
                    merged.append(clean);
            }
            for (const QString& value : existing)
            {
                if (!value.isEmpty() && !merged.contains(value))
                    merged.append(value);
            }
            if (!merged.isEmpty())
                environment.insert(key, merged.join(QLatin1Char(':')));
        }
    }

    QString application_support_root()
    {
        return core::paths::application_support_root();
    }

    QString default_prefix_root()
    {
        return core::paths::default_prefix_root();
    }

    QString default_log_root()
    {
        return core::paths::default_log_root();
    }

    QString resolve_wine_executable(const QString& selected_path)
    {
        const QString selected = selected_path.trimmed();
        if (selected.isEmpty())
        {
            QString system_wine = QStandardPaths::findExecutable(QStringLiteral("wine"));
            if (system_wine.isEmpty())
                system_wine = QStandardPaths::findExecutable(QStringLiteral("wine64"));
            return system_wine;
        }

        const QFileInfo supplied(selected);
        if (supplied.isFile() && supplied.isExecutable())
            return supplied.absoluteFilePath();

        if (!supplied.isDir())
            return {};

        const QDir root(supplied.absoluteFilePath());
        for (const QString& relative : candidate_relative_paths())
        {
            const QFileInfo candidate(root.filePath(relative));
            if (candidate.isFile() && candidate.isExecutable())
                return candidate.absoluteFilePath();
        }
        return {};
    }

    QString runtime_root_for_executable(const QString& executable)
    {
        const QString absolute = QFileInfo(executable).absoluteFilePath();
        const int app_end = absolute.indexOf(QStringLiteral(".app/"), 0, Qt::CaseInsensitive);
        if (app_end >= 0)
            return absolute.left(app_end + 4);

        QDir directory = QFileInfo(absolute).dir();
        if (directory.dirName().compare(QStringLiteral("bin"), Qt::CaseInsensitive) == 0)
        {
            directory.cdUp();
            return directory.absolutePath();
        }
        return QFileInfo(absolute).dir().absolutePath();
    }

    QStringList executable_architectures(const QString& executable)
    {
#if defined(Q_OS_MACOS)
        if (executable.isEmpty() || is_script(executable))
            return {};
        QProcess process;
        process.start(QStringLiteral("/usr/bin/lipo"),
                      {QStringLiteral("-archs"), executable});
        if (!process.waitForStarted(2000) || !process.waitForFinished(3000)
            || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        {
            return {};
        }
        return QString::fromUtf8(process.readAllStandardOutput())
            .simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
#else
        Q_UNUSED(executable);
        return {};
#endif
    }

    bool executable_requires_rosetta(const QString& executable)
    {
#if defined(Q_OS_MACOS)
        if (!is_apple_silicon_host() || executable.isEmpty() || is_script(executable))
            return false;
        const QStringList architectures = executable_architectures(executable);
        const bool has_apple_silicon_slice = architectures.contains(QStringLiteral("arm64"))
            || architectures.contains(QStringLiteral("arm64e"));
        return architectures.contains(QStringLiteral("x86_64"))
            && !has_apple_silicon_slice;
#else
        Q_UNUSED(executable);
        return false;
#endif
    }

    bool rosetta_is_available()
    {
#if defined(Q_OS_MACOS)
        if (!is_apple_silicon_host())
            return true;
        QProcess process;
        process.start(QStringLiteral("/usr/bin/arch"),
                      {QStringLiteral("-x86_64"), QStringLiteral("/usr/bin/true")});
        return process.waitForStarted(2500) && process.waitForFinished(5000)
            && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
#else
        return true;
#endif
    }

    RuntimeProbe probe_runtime(const QString& selected_path, const int timeout_ms)
    {
        RuntimeProbe result;
        result.selected_path = selected_path;
        result.apple_silicon_host = is_apple_silicon_host();
        result.executable = resolve_wine_executable(selected_path);
        result.exists = !result.executable.isEmpty() && QFileInfo(result.executable).isFile();
        result.executable_file = result.exists && QFileInfo(result.executable).isExecutable();
        if (!result.executable_file)
        {
            result.failure = QStringLiteral(
                "No executable Wine entry point was found in this selection.");
            return result;
        }

        result.runtime_root = runtime_root_for_executable(result.executable);
        result.architectures = executable_architectures(result.executable);
        result.requires_rosetta = executable_requires_rosetta(result.executable);
        result.rosetta_available = rosetta_is_available();
        if (result.requires_rosetta && !result.rosetta_available)
        {
            result.failure = QStringLiteral(
                "This Wine installation is Intel-only and Rosetta is not installed.");
            return result;
        }

        QString probe_failure;
        result.version = run_version_probe(result.executable, timeout_ms, &probe_failure);
        if (result.version.isEmpty())
        {
            result.failure = probe_failure.isEmpty()
                ? QStringLiteral(
                      "Wine could not complete its version check. It may be quarantined, incomplete, or missing a dependency.")
                : probe_failure;
            return result;
        }

        result.usable = true;
        return result;
    }

    QString prepare_host_launch(const QString& executable, QStringList& arguments)
    {
        if (executable.isEmpty())
            return {};
#if defined(Q_OS_MACOS)
        if (executable_requires_rosetta(executable))
        {
            arguments.prepend(executable);
            arguments.prepend(QStringLiteral("-x86_64"));
            return QStringLiteral("/usr/bin/arch");
        }
#endif
        return executable;
    }

    void apply_runtime_environment(QProcessEnvironment& environment,
                                   const QString& executable)
    {
#if defined(Q_OS_MACOS)
        const QString root = runtime_root_for_executable(executable);
        const QFileInfo executable_info(executable);
        prepend_path(environment, QStringLiteral("PATH"),
                     {executable_info.dir().absolutePath()});
        prepend_path(environment, QStringLiteral("DYLD_FALLBACK_LIBRARY_PATH"), {
            QDir(root).filePath(QStringLiteral("lib")),
            QDir(root).filePath(QStringLiteral("lib64")),
            QDir(root).filePath(QStringLiteral("Contents/Resources/wine/lib")),
            QDir(root).filePath(QStringLiteral("Contents/Resources/wine/lib64")),
            QDir(root).filePath(QStringLiteral("Contents/Resources/wine/lib/wine")),
            QDir(root).filePath(QStringLiteral("Contents/Resources/wine/lib64/wine")),
            QDir(root).filePath(QStringLiteral("Contents/Resources/Libraries/Wine/lib")),
            QDir(root).filePath(QStringLiteral("Contents/Resources/Libraries/Wine/lib64")),
            QDir(root).filePath(QStringLiteral("Contents/Resources/Libraries/Wine/lib/wine")),
            QDir(root).filePath(QStringLiteral("Contents/Resources/Libraries/Wine/lib64/wine")),
            QDir(root).filePath(QStringLiteral("Contents/SharedSupport/CrossOver/lib")),
            QDir(root).filePath(QStringLiteral("Contents/SharedSupport/CrossOver/lib64")),
            QDir(root).filePath(QStringLiteral("Contents/SharedSupport/CrossOver/lib/wine")),
            QDir(root).filePath(QStringLiteral("Contents/SharedSupport/CrossOver/lib64/wine"))
        });

        const QString crossover_marker = QStringLiteral("/Contents/SharedSupport/CrossOver/");
        const int crossover_offset = executable.indexOf(
            crossover_marker, 0, Qt::CaseInsensitive);
        if (crossover_offset >= 0)
        {
            const QString cx_root = executable.left(
                crossover_offset + crossover_marker.size() - 1);
            environment.insert(QStringLiteral("CX_ROOT"), cx_root);
        }
#else
        Q_UNUSED(environment);
        Q_UNUSED(executable);
#endif
    }

    bool request_rosetta_install_prompt()
    {
#if defined(Q_OS_MACOS)
        if (!is_apple_silicon_host() || rosetta_is_available())
            return true;
        return QProcess::startDetached(
            QStringLiteral("/usr/bin/arch"),
            {QStringLiteral("-x86_64"), QStringLiteral("/usr/bin/true")});
#else
        return false;
#endif
    }
}
