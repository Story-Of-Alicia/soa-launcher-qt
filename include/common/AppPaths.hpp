#pragma once

#include <QString>

namespace core::paths
{
    [[nodiscard]] QString application_support_root();
    [[nodiscard]] QString default_prefix_root();
    [[nodiscard]] QString default_log_root();
}
