#include "runtime/WineRegistry.hpp"
#include "config/Config.hpp"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include <mutex>

namespace soa::runtime
{
    QString winetricks_path()
    {
        const QString configured = soa::config::Config::instance().winetricks_binary();
        if (!configured.isEmpty())
        {
            if (QFileInfo(configured).isAbsolute())
                return QFileInfo(configured).isExecutable() ? configured : QString {};

            const QString found = QStandardPaths::findExecutable(configured);
            if (!found.isEmpty()) return found;
        }

        return QStandardPaths::findExecutable("winetricks");
    }

    QString umu_path()
    {
        const QString configured = soa::config::Config::instance().umu_binary();
        if (!configured.isEmpty())
        {
            if (QFileInfo(configured).isAbsolute())
                return QFileInfo(configured).isExecutable() ? configured : QString {};

            const QString found = QStandardPaths::findExecutable(configured);
            if (!found.isEmpty())
                return found;
        }

        QString found = QStandardPaths::findExecutable(QStringLiteral("umu-run"));
        if (!found.isEmpty())
            return found;

        const QString local = QDir::home().filePath(QStringLiteral(".local/bin/umu-run"));
        return QFileInfo(local).isExecutable() ? local : QString {};
    }

    bool winetricks_available()
    {
        const QString path = winetricks_path();
        return !path.isEmpty() && QFileInfo(path).isExecutable();
    }

    bool umu_available()
    {
        const QString path = umu_path();
        return !path.isEmpty() && QFileInfo(path).isExecutable();
    }

    bool umu_executable_supports_managed_folders(const QString& path)
    {
#if defined(Q_OS_MACOS)
        Q_UNUSED(path);
        return false;
#else
        const QFileInfo info(path);
        if (path.isEmpty() || !info.isFile() || !info.isExecutable())
            return false;

        struct Cache
        {
            QString path;
            qint64 size {-1};
            qint64 modified {-1};
            bool supported {};
        };
        static std::mutex mutex;
        static Cache cache;

        const qint64 size = info.size();
        const qint64 modified = info.lastModified().toMSecsSinceEpoch();
        std::lock_guard lock(mutex);
        if (cache.path == path && cache.size == size && cache.modified == modified)
            return cache.supported;

        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(path, {QStringLiteral("--version")});

        bool supported = false;
        if (process.waitForStarted(1500))
        {
            if (process.waitForFinished(2500))
            {
                if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0)
                {
                    const QString output = QString::fromUtf8(process.readAll());
                    const QRegularExpression expression(
                        QStringLiteral(
                            R"(umu-launcher\s+version\s+(\d+)\.(\d+)(?:\.(\d+))?)"),
                        QRegularExpression::CaseInsensitiveOption);
                    const QRegularExpressionMatch match = expression.match(output);
                    if (match.hasMatch())
                    {
                        const int major = match.captured(1).toInt();
                        const int minor = match.captured(2).toInt();
                        supported = major > 1 || (major == 1 && minor >= 4);
                    }
                }
            }
            else
            {
                process.kill();
                process.waitForFinished(1000);
            }
        }

        cache = {path, size, modified, supported};
        return supported;
#endif
    }

    bool umu_supports_managed_folders()
    {
        return umu_executable_supports_managed_folders(umu_path());
    }

}
