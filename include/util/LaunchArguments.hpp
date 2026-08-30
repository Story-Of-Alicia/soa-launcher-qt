#pragma once

#include <QString>
#include <QStringList>

namespace util::launch_arguments
{
    struct ValidationResult
    {
        bool valid {};
        QString error;
        QStringList arguments;
        QStringList environment_entries;
        QString developer_id;
        QString developer_op;
    };

    [[nodiscard]] bool developer_mode_enabled();
    ValidationResult validate(const QString& raw);
}
