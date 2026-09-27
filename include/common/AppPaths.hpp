#pragma once

#include <QString>

namespace soa::common::paths
{
    [[nodiscard]] QString application_support_root();
    [[nodiscard]] QString default_prefix_root();
    [[nodiscard]] QString default_proton_compat_data_root();
    [[nodiscard]] QString default_log_root();
}
