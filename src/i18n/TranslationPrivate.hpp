// Story of Alicia Launcher
// SPDX-FileCopyrightText: 2026 Story of Alicia contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional terms apply, see ADDITIONAL_TERMS.md.

#pragma once

#include <QString>

namespace soa::i18n::detail
{
    void set_catalog_active(bool active);
    [[nodiscard]] QString translated(const QString& source);
}
