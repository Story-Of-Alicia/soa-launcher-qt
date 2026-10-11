#include "common/AppPaths.hpp"

#include <QDir>
#include <QStandardPaths>

namespace soa::common::paths
{
    QString application_support_root()
    {
#if defined(Q_OS_MACOS)
        QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        if (base.isEmpty())
            base = QDir::home().filePath(QStringLiteral("Library/Application Support"));
        return QDir(base).filePath(QStringLiteral("Story of Alicia"));
#else
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#endif
    }

    QString default_prefix_root()
    {
#if defined(Q_OS_MACOS)
        return QDir(application_support_root()).filePath(QStringLiteral("prefixes/shared"));
#else
        return QDir(application_support_root()).filePath(QStringLiteral("prefixes/wine"));
#endif
    }

    QString default_proton_compat_data_root()
    {
#if defined(Q_OS_MACOS)
        return default_prefix_root();
#else
        return QDir(application_support_root()).filePath(QStringLiteral("prefixes/proton"));
#endif
    }

    QString default_log_root()
    {
        return QDir(application_support_root()).filePath(QStringLiteral("logs"));
    }
}
