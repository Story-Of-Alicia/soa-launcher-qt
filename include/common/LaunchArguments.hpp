#pragma once

#include <QString>
#include <QStringList>

namespace soa::common::launch_arguments
{
    struct ValidationResult
    {
        bool valid {};
        QString error;
        QStringList arguments;
        QStringList environment_entries;
    };

    ValidationResult validate(const QString& raw);
}
