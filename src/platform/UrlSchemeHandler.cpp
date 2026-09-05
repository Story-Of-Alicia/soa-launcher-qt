#include "platform/UrlSchemeHandler.hpp"

#include <QtGlobal>

#if defined(Q_OS_LINUX)

#include "common/DesktopEntry.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QString>
#include <QTextStream>

#include <spdlog/spdlog.h>

namespace
{
    QString desktop_exec_value(const QString& executable)
    {
        return util::desktop_entry::quoted_exec_argument(executable) + QStringLiteral(" %u");
    }

    QByteArray url_handler_desktop_contents(const QString& executable)
    {
        QString text;
        QTextStream stream(&text);
        stream << "[Desktop Entry]\n"
               << "Type=Application\n"
               << "Name=Story Of Alicia URL Handler\n"
               << "Comment=Handles Story Of Alicia authentication callbacks\n"
               << "Exec=" << desktop_exec_value(executable) << "\n"
               << "Icon=soa-launcher\n"
               << "Terminal=false\n"
               << "NoDisplay=true\n"
               << "MimeType=x-scheme-handler/soa;\n";
        return text.toUtf8();
    }
}

namespace core::platform
{
    void register_launcher_url_scheme()
    {
        const QString applications_dir =
            QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
        if (applications_dir.isEmpty() || !QDir().mkpath(applications_dir))
        {
            SPDLOG_WARN("could not prepare the user applications directory for URL registration");
            return;
        }

        QString executable = qEnvironmentVariable("APPIMAGE");
        if (executable.isEmpty())
            executable = QCoreApplication::applicationFilePath();

        const QString desktop_name = QStringLiteral("soa-launcher-url-handler.desktop");
        const QString desktop_path = QDir(applications_dir).filePath(desktop_name);
        const QByteArray desired = url_handler_desktop_contents(executable);

        QFile existing(desktop_path);
        bool needs_write = true;
        if (existing.open(QIODevice::ReadOnly))
            needs_write = existing.readAll() != desired;

        if (needs_write)
        {
            QSaveFile desktop_file(desktop_path);
            if (!desktop_file.open(QIODevice::WriteOnly | QIODevice::Text)
                || desktop_file.write(desired) != desired.size()
                || !desktop_file.commit())
            {
                SPDLOG_WARN("could not write soa URL handler desktop file: {}",
                            desktop_path.toStdString());
                return;
            }
        }

        const QString xdg_mime = QStandardPaths::findExecutable(QStringLiteral("xdg-mime"));
        if (xdg_mime.isEmpty())
        {
            SPDLOG_WARN("xdg-mime is unavailable; soa URL handler was not selected");
            return;
        }

        QProcess process;
        process.start(
            xdg_mime,
            {QStringLiteral("default"), desktop_name, QStringLiteral("x-scheme-handler/soa")});
        if (!process.waitForStarted(3000)
            || !process.waitForFinished(5000)
            || process.exitStatus() != QProcess::NormalExit
            || process.exitCode() != 0)
        {
            SPDLOG_WARN("xdg-mime failed to register the soa URL handler");
        }
    }
}

#else

namespace core::platform
{
    void register_launcher_url_scheme()
    {
    }
}

#endif
