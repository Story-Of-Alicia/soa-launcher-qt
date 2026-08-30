#pragma once

#include <QString>

namespace core::wine
{
    class AliciaLogHook final
    {
    public:
        struct Preparation
        {
            bool requested {};
            bool available {};
            QString injector_host_path;
            QString injector_windows_path;
            QString log_host_path;
            QString log_windows_path;
            QString audio_host_host_path;
            QString failure;
        };

        [[nodiscard]] static Preparation prepare(const QString& prefix,
                                                 const QString& diagnostic_run_directory,
                                                 bool force_windowed,
                                                 bool audio_required = false);
    };
}
