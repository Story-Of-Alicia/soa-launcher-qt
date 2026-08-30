#pragma once

#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <functional>

class QTimer;

namespace core::wine
{
    enum class CommandOutcome
    {
        Success,
        NonZeroExit,
        FailedToStart,
        Crashed,
        TimedOut,
        Cancelled
    };

    struct command_result
    {
        CommandOutcome outcome {CommandOutcome::FailedToStart};
        bool started {};
        int exit_code {-1};
        bool crashed {};
        bool output_truncated {};
        QString output;
        QString error_message;

        [[nodiscard]] bool ok() const
        {
            return outcome == CommandOutcome::Success;
        }
    };

    [[nodiscard]] QString command_outcome_name(CommandOutcome outcome);
    [[nodiscard]] QString command_failure_message(const QString& action,
                                                  const command_result& result);
    [[nodiscard]] QString
    redact_sensitive_text(QString text, const QStringList& sensitive_values = QStringList {});
    [[nodiscard]] QStringList
    redacted_command_args(QStringList arguments,
                          const QStringList& sensitive_values = QStringList {});

    class ProcessRunner final : public QObject
    {
    public:
        struct Request
        {
            QString program;
            QStringList arguments;
            QProcessEnvironment environment {QProcessEnvironment::systemEnvironment()};
            QString working_directory;
            QStringList sensitive_values;
            int timeout_ms {};
            bool prevent_system_sleep {};
            std::function<void(qint64)> started;
            std::function<void(const QString&)> output;
        };

        using Completion = std::function<void(const command_result&)>;

        explicit ProcessRunner(QObject* parent = nullptr);
        ~ProcessRunner() override;

        [[nodiscard]] bool start(Request request, Completion completion = Completion {});
        [[nodiscard]] bool is_busy() const;
        [[nodiscard]] qint64 process_id() const;
        [[nodiscard]] QProcess::ProcessState state() const;
        [[nodiscard]] command_result snapshot() const;

        void cancel();
        void terminate(CommandOutcome outcome = CommandOutcome::Cancelled);

        [[nodiscard]] static bool prepare_host_invocation(const QString& program,
                                                          const QStringList& arguments,
                                                          const QProcessEnvironment& environment,
                                                          QString& executable,
                                                          QStringList& launch_arguments,
                                                          QProcessEnvironment& launch_environment);

    private:
        void handle_output();
        void finish(CommandOutcome outcome, int exit_code, QProcess::ExitStatus exit_status,
                    const QString& error_message = QString {});

        QProcess* process_ {};
        QTimer* timeout_timer_ {};
        QTimer* force_kill_timer_ {};
        Request request_;
        Completion completion_;
        command_result current_;
        CommandOutcome forced_outcome_ {CommandOutcome::Success};
        qint64 force_kill_process_id_ {-1};
        bool active_ {};
        bool terminal_emitted_ {};
    };
}
