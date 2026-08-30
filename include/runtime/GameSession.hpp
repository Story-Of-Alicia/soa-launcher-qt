#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>

#include <functional>
#include <optional>

#include "common/GameVersion.hpp"
#include "runtime/MacLaunchDiagnostics.hpp"
#include "runtime/ProcessRunner.hpp"
#include "runtime/WineProcess.hpp"

class QTimer;

namespace core::wine
{
    class RuntimeLocator;

    enum class GamePhase
    {
        Idle,
        Preflight,
        CleaningPrefix,
        FirstLaunchSetup,
        Launching,
        Running,
        MonitoringUncertain,
        Finished
    };

    [[nodiscard]] QString game_phase_name(GamePhase phase);
    [[nodiscard]] bool is_valid_game_transition(GamePhase from, GamePhase to);

    class GameSession final : public QObject
    {
    public:
        struct Callbacks
        {
            std::function<void(const command_result&)> command_finished;
            std::function<void(const QString&, const QString&)> fail_user;
            std::function<void(const QString&)> user_notice;
            std::function<void(core::game::GameVersion)> game_starting;
            std::function<void(core::game::GameVersion)> game_started;
            std::function<void(core::game::GameVersion, int, bool)> game_exited;
            std::function<void(const QString&, double, bool)> working;
            std::function<void(const QString&)> done;
            std::function<void(const QString&)> failed;
        };

        GameSession(RuntimeLocator& runtime, ProcessRunner& runner, Callbacks callbacks,
                    QObject* parent = nullptr);
        ~GameSession() override;

        [[nodiscard]] GamePhase phase() const;
        [[nodiscard]] bool is_busy() const;
        [[nodiscard]] bool is_game_running() const;

        void run_game(const QString& user, const QString& token);
        void detect_existing_game();
        void cancel();

    private:
        enum class WrapperState
        {
            NotStarted,
            Running,
            Finished
        };

        enum class SessionOrigin
        {
            Started,
            Restored
        };

        struct PendingLaunch
        {
            core::game::GameVersion version {core::game::GameVersion::Playtest};
            QString user;
            QString token;
            QString game_directory;
            QString executable_path;
        };

        [[nodiscard]] bool transition(GamePhase next);
        void clear_session_state();
        void stop_probe();
        void begin_preflight(PendingLaunch launch);
        void clean_stale_prefix_processes();
        void wait_for_prefix_shutdown();
        void begin_first_launch_setup();
        void handle_first_launch_query(const command_result& result);
        [[nodiscard]] bool write_first_launch_registry(const PendingLaunch& launch,
                                                       const QString& query_output,
                                                       QString& registry_file,
                                                       bool& needs_import) const;
        void launch_pending_game();
        void launch_game_process();
        [[nodiscard]] bool run_stage_command(ProcessRunner::Request request,
                                             GamePhase expected_phase,
                                             std::function<void(const command_result&)> completion);

        [[nodiscard]] bool
        start_game_probe(std::function<void(bool, std::optional<WindowsProcessInfo>)> completion);
        [[nodiscard]] bool start_host_game_probe(
            std::function<void(bool, std::optional<WindowsProcessInfo>)> completion);
        [[nodiscard]] bool start_game_lifecycle_probe(
            std::function<void(bool, std::optional<WindowsProcessInfo>)> completion);
        void capture_game_probe_context();
        void clear_game_probe_context();
        void handle_game_probe_output();
        void begin_game_verification();
        void poll_game_process();
        void on_probe_result(bool ok, std::optional<WindowsProcessInfo> info);
        void on_probe_failure();
        void on_process_seen(const WindowsProcessInfo& info);
        void on_process_missing();
        void on_still_launching();
        void attach_to_game_process(const WindowsProcessInfo& info, SessionOrigin origin);
        void refresh_game_host_diagnostics();
        void handle_wrapper_finished(const command_result& result, quint64 generation);
        void fail_game_launch(const QString& message);
        void stop_monitoring_uncertain(const QString& message);
        void finish_game_session(int exit_code, bool crashed);
        [[nodiscard]] command_result current_session_result() const;
        [[nodiscard]] QString diagnostic_suffix() const;

        RuntimeLocator& runtime_;
        ProcessRunner& runner_;
        Callbacks callbacks_;
        MacLaunchDiagnostics diagnostics_;
        QProcess* probe_process_ {};
        QTimer* monitor_timer_ {};
        QTimer* probe_timeout_timer_ {};
        GamePhase phase_ {GamePhase::Idle};
        WrapperState wrapper_state_ {WrapperState::NotStarted};
        SessionOrigin origin_ {SessionOrigin::Started};
        std::optional<PendingLaunch> launch_;
        QString pending_registry_file_;
        QByteArray probe_output_;
        QString probe_program_snapshot_;
        QString probe_prefix_snapshot_;
        QProcessEnvironment probe_environment_snapshot_;
        std::function<void(bool, std::optional<WindowsProcessInfo>)> probe_completion_;
        bool probe_host_mode_ {};
        bool probe_context_valid_ {};
        WindowsProcessInfo tracked_game_process_;
        WindowsProcessInfo tracked_host_process_;
        core::game::GameVersion tracked_version_ {core::game::GameVersion::Playtest};
        command_result last_stage_result_;
        command_result wrapper_result_;
        qint64 wrapper_finished_at_ms_ {-1};
        int verification_attempts_ {};
        int probe_failures_ {};
        int missing_probes_ {};
        quint64 generation_ {};
        bool diagnostic_sample_armed_ {};
    };
}
