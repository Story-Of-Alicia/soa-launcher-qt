// Story of Alicia Launcher
// SPDX-FileCopyrightText: 2026 Story of Alicia contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional terms apply, see ADDITIONAL_TERMS.md.

#pragma once

#include "common/Status.hpp"
#include "network/Courier.h"

namespace soa::network
{
    struct DownloadStatus
    {
        qulonglong           operation_id {0};
        common::status::Status       base;
        courier_phase        phase      {courier_phase_preparing};
        courier_result       result     {courier_result_completed};
        qulonglong           received   {0};
        qulonglong           total      {0};
        qulonglong           speed      {0};
        int                  file_index {0};
        int                  file_count {0};
    };
}
