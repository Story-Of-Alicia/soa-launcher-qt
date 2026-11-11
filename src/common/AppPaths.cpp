// Story of Alicia Launcher
// SPDX-FileCopyrightText: 2026 Story of Alicia contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional terms apply, see ADDITIONAL_TERMS.md.

#include "common/AppPaths.hpp"

#include <QDir>
#include <QStandardPaths>

namespace soa::common::paths
{
    QString application_support_root()
    {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }

    QString default_prefix_root()
    {
        return QDir(application_support_root()).filePath(QStringLiteral("prefixes/wine"));
    }

    QString default_proton_compat_data_root()
    {
        return QDir(application_support_root()).filePath(QStringLiteral("prefixes/proton"));
    }

    QString default_log_root()
    {
        return QDir(application_support_root()).filePath(QStringLiteral("logs"));
    }
}
