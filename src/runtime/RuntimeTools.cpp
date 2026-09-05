#include "runtime/WineRegistry.hpp"
#include "config/Config.hpp"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace core::wine
{
    QString winetricks_path()
    {
        const QString configured = util::config::Config::instance().winetricks_binary();
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
        const QString configured = util::config::Config::instance().umu_binary();
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

}
