// Story of Alicia Launcher
// SPDX-FileCopyrightText: 2026 Story of Alicia contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional terms apply, see ADDITIONAL_TERMS.md.

#include "common/DesktopEntry.hpp"

namespace soa::common::desktop_entry
{
    QString quoted_exec_argument(const QString& value)
    {
        QString escaped = value;
        escaped.replace(QStringLiteral("%"), QStringLiteral("%%"));
        escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
        escaped.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
        escaped.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
        escaped.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
        escaped.replace(QStringLiteral("\""), QStringLiteral("\\\""));
        return QStringLiteral("\"") + escaped + QStringLiteral("\"");
    }
}
