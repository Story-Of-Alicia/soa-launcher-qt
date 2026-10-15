// Story of Alicia Launcher
// SPDX-FileCopyrightText: 2026 Story of Alicia contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional terms apply, see ADDITIONAL_TERMS.md.

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
