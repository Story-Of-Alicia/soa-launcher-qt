// Story of Alicia Launcher
// SPDX-FileCopyrightText: 2026 Story of Alicia contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional terms apply, see ADDITIONAL_TERMS.md.

#pragma once

#include <QString>

namespace soa::common::paths
{
    [[nodiscard]] QString application_support_root();
    [[nodiscard]] QString default_prefix_root();
    [[nodiscard]] QString default_proton_compat_data_root();
    [[nodiscard]] QString default_log_root();
}
