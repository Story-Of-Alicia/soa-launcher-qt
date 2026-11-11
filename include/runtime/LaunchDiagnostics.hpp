// Story of Alicia Launcher
// SPDX-FileCopyrightText: 2026 Story of Alicia contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional terms apply, see ADDITIONAL_TERMS.md.

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

#include "common/GameVersion.hpp"

namespace soa::runtime
{
    class LaunchDiagnostics final : public QObject
    {
    public:
        struct RuntimeContext
        {
            bool usable {};
            QString source;
            QString selector;
            QString executable;
            QString failure;
        };

        struct Configuration
        {
            bool supported {};
            bool enabled {};
            QString profile;
            QString wine_debug;
            RuntimeContext runtime;
        };

        struct ProcessSnapshot
        {
            bool session_active {};
            bool game_confirmed {};
            bool wrapper_running {};
        };

        using SnapshotProvider = std::function<ProcessSnapshot()>;

        explicit LaunchDiagnostics(QObject* parent = nullptr);
        ~LaunchDiagnostics() override;

        void reset();
        [[nodiscard]] Configuration begin(soa::common::game::GameVersion version, const QString& prefix,
                                          const QString& game_directory,
                                          const QString& executable_path,
                                          const QStringList& sensitive_values,
                                          SnapshotProvider snapshot_provider);
        void observe_output(const QString& chunk);
        void append_event(const QString& event, const QString& details = QString {});
        void write_effective_command(const QString& program, const QStringList& redacted_arguments,
                                     const QString& working_directory);
        void finish(const QString& outcome, int exit_code, bool crashed,
                    const QString& details = QString {});
        void append_footer_and_analyze(const QString& footer, const QString& executable_path);
        void close_log();

        [[nodiscard]] bool trace_has_fatal_failure() const;
        [[nodiscard]] qint64 elapsed_ms() const;
        [[nodiscard]] bool saw_present() const;
        [[nodiscard]] bool saw_draw() const;
        [[nodiscard]] QString diagnostic_path() const;
        [[nodiscard]] QString timeline_path() const;
        [[nodiscard]] QString run_directory() const;
        [[nodiscard]] Configuration configuration() const;

        [[nodiscard]] static QString wine_debug_value(bool diagnostics_enabled);
        [[nodiscard]] static QString registry_summary(const QString& prefix);

    private:
        class Impl;
        std::unique_ptr<Impl> d_;
    };
}
