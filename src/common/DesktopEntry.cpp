#include "common/DesktopEntry.hpp"

namespace util::desktop_entry
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
