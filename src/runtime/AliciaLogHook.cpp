#include "runtime/AliciaLogHook.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <spdlog/spdlog.h>

namespace core::wine
{
    namespace
    {
        constexpr auto k_injector = "SoaAliciaLogInjector.exe";
        constexpr auto k_hook = "SoaAliciaLogHook.dll";

        QString find_packaged_hook_directory()
        {
            QStringList candidates;
            const QString override = qEnvironmentVariable("SOA_ALICIA_LOG_HOOK_DIR").trimmed();
            if (!override.isEmpty())
                candidates.append(override);

            const QDir application(QCoreApplication::applicationDirPath());
            candidates.append(application.filePath(QStringLiteral("alicia-log-hook")));
#if defined(Q_OS_MACOS)
            candidates.append(application.filePath(QStringLiteral("../Resources/alicia-log-hook")));
#else
            candidates.append(
                application.filePath(QStringLiteral("../libexec/soa-launcher/alicia-log-hook")));
#endif

            for (const QString& candidate : candidates)
            {
                const QDir directory(QDir::cleanPath(candidate));
                if (QFileInfo(directory.filePath(QString::fromLatin1(k_injector))).isFile() &&
                    QFileInfo(directory.filePath(QString::fromLatin1(k_hook))).isFile())
                {
                    return directory.absolutePath();
                }
            }
            return {};
        }

        bool refresh_file(const QString& source, const QString& destination, QString& error)
        {
            if (QFileInfo::exists(destination) && !QFile::remove(destination))
            {
                error = QStringLiteral("Could not replace staged diagnostic component: %1")
                            .arg(destination);
                return false;
            }
            QFile source_file(source);
            if (!source_file.copy(destination))
            {
                error = QStringLiteral("Could not stage diagnostic component: %1")
                            .arg(source_file.errorString());
                return false;
            }
            QFile::setPermissions(destination,
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                      QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                                      QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                      QFileDevice::ExeOther);
            return true;
        }

        QString wine_z_path(QString host_path)
        {
            host_path = QDir::cleanPath(QFileInfo(host_path).absoluteFilePath());
            if (!host_path.startsWith(QLatin1Char('/')))
                return {};
            host_path.replace(QLatin1Char('/'), QLatin1Char('\\'));
            return QStringLiteral("Z:") + host_path;
        }
    }

    AliciaLogHook::Preparation AliciaLogHook::prepare(
        const QString& prefix, const QString& diagnostic_run_directory,
        const bool force_windowed,
        const bool audio_required)
    {
        Preparation result;
        const bool diagnostic_requested = !diagnostic_run_directory.isEmpty();
        result.requested = diagnostic_requested || force_windowed || audio_required;
        if (!result.requested)
            return result;
        if (diagnostic_requested)
        {
            const QFileInfo run_info(diagnostic_run_directory);
            if (!run_info.isDir() || !run_info.isWritable())
            {
                result.failure = QStringLiteral(
                    "The labeled diagnostic folder is not writable, so alicia.log cannot be created.");
                return result;
            }
        }

        const QString packaged_directory = find_packaged_hook_directory();
        if (packaged_directory.isEmpty())
        {
            result.failure = QStringLiteral(
                "The packaged Alicia injector and compatibility hook were not found.");
            return result;
        }

        const QString stage_directory = QDir(prefix).filePath(
            QStringLiteral("drive_c/StoryOfAliciaLauncher/alicia-log-hook"));
        if (!QDir().mkpath(stage_directory))
        {
            result.failure = QStringLiteral("Could not create the Alicia log-hook staging folder.");
            return result;
        }

        const QString source_injector =
            QDir(packaged_directory).filePath(QString::fromLatin1(k_injector));
        const QString source_hook =
            QDir(packaged_directory).filePath(QString::fromLatin1(k_hook));
        result.injector_host_path =
            QDir(stage_directory).filePath(QString::fromLatin1(k_injector));
        const QString staged_hook =
            QDir(stage_directory).filePath(QString::fromLatin1(k_hook));

        const QString source_audio_host =
            QDir(packaged_directory).filePath(QStringLiteral("soa-audio-host"));
        if (QFileInfo(source_audio_host).isFile())
        {
            const QString staged_audio_host =
                QDir(stage_directory).filePath(QStringLiteral("soa-audio-host"));
            QString ignored;
            if (refresh_file(source_audio_host, staged_audio_host, ignored))
            {
                QFile::setPermissions(staged_audio_host,
                                      QFile::permissions(staged_audio_host) |
                                          QFileDevice::ExeOwner |
                                          QFileDevice::ExeGroup |
                                          QFileDevice::ExeOther);
                result.audio_host_host_path = staged_audio_host;
            }
        }

        if (!refresh_file(source_injector, result.injector_host_path, result.failure) ||
            !refresh_file(source_hook, staged_hook, result.failure))
        {
            result.injector_host_path.clear();
            return result;
        }

        result.injector_windows_path =
            QStringLiteral("C:\\StoryOfAliciaLauncher\\alicia-log-hook\\") +
            QString::fromLatin1(k_injector);
        if (diagnostic_requested)
        {
            result.log_host_path =
                QDir(diagnostic_run_directory).filePath(QStringLiteral("alicia.log"));
            result.log_windows_path = wine_z_path(result.log_host_path);
            if (result.log_windows_path.isEmpty())
            {
                result.failure = QStringLiteral("Could not map the Alicia diagnostic log path to Wine.");
                result.injector_host_path.clear();
                result.injector_windows_path.clear();
                result.log_host_path.clear();
                return result;
            }
        }

        result.available = true;
        SPDLOG_INFO("Alicia hook staged from {} to {} (diagnostic_log={} force_windowed={})",
                    packaged_directory.toStdString(), stage_directory.toStdString(),
                    diagnostic_requested, force_windowed);
        return result;
    }
}
