// Story of Alicia Launcher
// SPDX-FileCopyrightText: 2026 Story of Alicia contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional terms apply, see ADDITIONAL_TERMS.md.

#pragma once

#include <QStringList>

namespace soa::runtime
{
    enum class GpuVendor
    {
        Amd,
        Nvidia,
        Intel,
        Apple,
        Unknown
    };

    enum class CpuArchitecture
    {
        X86_64,
        Arm64,
        Unknown
    };

    struct SystemProfile
    {
        GpuVendor gpu_vendor {GpuVendor::Unknown};
        CpuArchitecture cpu_architecture {CpuArchitecture::Unknown};
        QStringList gpu_descriptions;
        bool vulkan_likely {};
        bool rosetta_available {};
    };

    [[nodiscard]] SystemProfile detect_system_profile();
}
