#include "runtime/ProcessRunner.hpp"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#include <utility>

#include "runtime/MacWineRuntime.hpp"
#include <spdlog/spdlog.h>

namespace core::wine
{
    namespace
    {
        constexpr qsizetype k_max_captured_output = 1024 * 1024;
        constexpr qsizetype k_detail_lines = 8;
        constexpr qsizetype k_detail_characters = 1800;
    }

    QString command_outcome_name(const CommandOutcome outcome)
    {
        switch (outcome)
        {
        case CommandOutcome::Success:
            return QStringLiteral("success");
        case CommandOutcome::NonZeroExit:
            return QStringLiteral("non-zero exit");
        case CommandOutcome::FailedToStart:
            return QStringLiteral("failed to start");
        case CommandOutcome::Crashed:
            return QStringLiteral("crashed");
        case CommandOutcome::TimedOut:
            return QStringLiteral("timed out");
        case CommandOutcome::Cancelled:
            return QStringLiteral("cancelled");
        }
        return QStringLiteral("unknown");
    }

    QString redact_sensitive_text(QString text, const QStringList& sensitive_values)
    {
        static const QRegularExpression operation_token(
            QStringLiteral(R"((?i)(-OP\s+)\[[^\]]*\])"));
        text.replace(operation_token, QStringLiteral("\\1[REDACTED]"));

        for (const QString& value : sensitive_values)
        {
            if (!value.isEmpty())
                text.replace(value, QStringLiteral("[REDACTED]"));
        }
        return text;
    }

    QStringList redacted_command_args(QStringList arguments, const QStringList& sensitive_values)
    {
        for (qsizetype index = 0; index < arguments.size(); ++index)
        {
            if (arguments[index].compare(QStringLiteral("-OP"), Qt::CaseInsensitive) == 0 &&
                index + 1 < arguments.size())
            {
                arguments[index + 1] = QStringLiteral("[REDACTED]");
            }
            arguments[index] = redact_sensitive_text(arguments[index], sensitive_values);
        }
        return arguments;
    }

    QString command_failure_message(const QString& action, const command_result& result)
    {
        QString message;
        if (result.exit_code >= 0)
        {
            message = QStringLiteral("%1 (%2, exit %3).")
                          .arg(action, command_outcome_name(result.outcome))
                          .arg(result.exit_code);
        }
        else
        {
            message = QStringLiteral("%1 (%2).").arg(action, command_outcome_name(result.outcome));
        }

        QStringList details;
        const QString process_error = result.error_message.trimmed();
        if (!process_error.isEmpty())
            details.append(process_error);

        for (const QString& raw_line : result.output.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
        {
            const QString line = raw_line.trimmed();
            if (line.isEmpty() ||
                QRegularExpression(QStringLiteral(R"(^[-=]{8,}$)")).match(line).hasMatch())
            {
                continue;
            }
            details.append(line.left(512));
        }

        if (details.size() > k_detail_lines)
            details = details.mid(details.size() - k_detail_lines);
        if (result.output_truncated)
            details.prepend(QStringLiteral("[earlier command output omitted]"));

        QString detail = details.join(QLatin1Char('\n'));
        if (detail.size() > k_detail_characters)
        {
            detail =
                QStringLiteral("[earlier detail omitted]\n") + detail.right(k_detail_characters);
        }
        if (!detail.isEmpty())
        {
            message += QStringLiteral("\n\nLast command output:\n") + detail;
        }
        message += QStringLiteral("\n\nSee launcher.log for the complete command output.");
        return message;
    }

    ProcessRunner::ProcessRunner(QObject* parent) : QObject(parent)
    {
        process_ = new QProcess(this);
        process_->setProcessChannelMode(QProcess::MergedChannels);
        timeout_timer_ = new QTimer(this);
        timeout_timer_->setSingleShot(true);
        force_kill_timer_ = new QTimer(this);
        force_kill_timer_->setSingleShot(true);

        connect(process_, &QProcess::started, this,
                [this]()
                {
                    if (!active_ || terminal_emitted_)
                        return;
                    current_.started = true;
                    if (request_.started)
                        request_.started(process_->processId());
                });
        connect(process_, &QProcess::readyReadStandardOutput, this, [this]() { handle_output(); });
        connect(process_, &QProcess::errorOccurred, this,
                [this](const QProcess::ProcessError error)
                {
                    if (!active_ || terminal_emitted_)
                        return;
                    if (error == QProcess::FailedToStart)
                    {
                        const CommandOutcome outcome = forced_outcome_ == CommandOutcome::Success
                                                           ? CommandOutcome::FailedToStart
                                                           : forced_outcome_;
                        finish(outcome, -1, QProcess::NormalExit, process_->errorString());
                        return;
                    }
                    SPDLOG_WARN("process error: {}", process_->errorString().toStdString());
                });
        connect(process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this](const int exit_code, const QProcess::ExitStatus exit_status)
                {
                    if (!active_ || terminal_emitted_)
                    {
                        handle_output();
                        return;
                    }

                    CommandOutcome outcome = forced_outcome_;
                    if (outcome == CommandOutcome::Success)
                    {
                        if (exit_status == QProcess::CrashExit)
                            outcome = CommandOutcome::Crashed;
                        else if (exit_code != 0)
                            outcome = CommandOutcome::NonZeroExit;
                    }
                    finish(outcome, exit_code, exit_status);
                });
        connect(timeout_timer_, &QTimer::timeout, this,
                [this]()
                {
                    if (!active_ || terminal_emitted_)
                        return;
                    forced_outcome_ = CommandOutcome::TimedOut;
                    SPDLOG_ERROR("command timed out: {}", process_->program().toStdString());
                    if (process_->state() == QProcess::Starting)
                    {
                        process_->kill();
                        return;
                    }
                    force_kill_process_id_ = process_->processId();
                    process_->terminate();
                    force_kill_timer_->start(3000);
                });
        connect(force_kill_timer_, &QTimer::timeout, this,
                [this]()
                {
                    const qint64 expected_pid = force_kill_process_id_;
                    force_kill_process_id_ = -1;
                    if (expected_pid > 0 && process_->state() != QProcess::NotRunning &&
                        process_->processId() == expected_pid)
                    {
                        process_->kill();
                    }
                });
    }

    ProcessRunner::~ProcessRunner()
    {
        active_ = false;
        terminal_emitted_ = true;
        completion_ = {};
        request_.started = {};
        request_.output = {};
        timeout_timer_->stop();
        force_kill_timer_->stop();
        QObject::disconnect(process_, nullptr, this, nullptr);
        if (process_->state() != QProcess::NotRunning)
        {
            process_->kill();
            process_->waitForFinished(1000);
        }
    }

    bool ProcessRunner::prepare_host_invocation(const QString& program,
                                                const QStringList& arguments,
                                                const QProcessEnvironment& environment,
                                                QString& executable, QStringList& launch_arguments,
                                                QProcessEnvironment& launch_environment)
    {
        const QFileInfo supplied(program);
        executable = supplied.isAbsolute() ? supplied.absoluteFilePath()
                                           : QStandardPaths::findExecutable(program);
        if (executable.isEmpty() || !QFileInfo(executable).isFile() ||
            !QFileInfo(executable).isExecutable())
        {
            SPDLOG_ERROR("executable not found or not runnable: {}", program.toStdString());
            return false;
        }

        const QString runtime_executable = executable;
        launch_arguments = arguments;
        launch_environment = environment;
#if defined(Q_OS_MACOS)
        if (macos::executable_requires_rosetta(runtime_executable) &&
            !macos::rosetta_is_available())
        {
            SPDLOG_ERROR("Rosetta is required to launch {}", runtime_executable.toStdString());
            return false;
        }
        macos::apply_runtime_environment(launch_environment, runtime_executable);
        executable = macos::prepare_host_launch(runtime_executable, launch_arguments);
        if (executable.isEmpty())
            return false;
#endif
        return true;
    }

    bool ProcessRunner::start(Request request, Completion completion)
    {
        if (active_)
        {
            SPDLOG_WARN("attempted to start {} while another process is active",
                        request.program.toStdString());
            return false;
        }

        QString executable;
        QStringList launch_arguments;
        QProcessEnvironment launch_environment;
        if (!prepare_host_invocation(request.program, request.arguments, request.environment,
                                     executable, launch_arguments, launch_environment))
        {
            return false;
        }

#if defined(Q_OS_MACOS)
        if (request.prevent_system_sleep &&
            QFileInfo(QStringLiteral("/usr/bin/caffeinate")).isExecutable())
        {
            launch_arguments.prepend(executable);
            launch_arguments.prepend(QStringLiteral("-dimsu"));
            executable = QStringLiteral("/usr/bin/caffeinate");
        }
#endif

        request_ = std::move(request);
        completion_ = std::move(completion);
        current_ = {};
        forced_outcome_ = CommandOutcome::Success;
        force_kill_timer_->stop();
        force_kill_process_id_ = -1;
        terminal_emitted_ = false;
        active_ = true;

        process_->setProcessEnvironment(launch_environment);
        process_->setWorkingDirectory(
            request_.working_directory.isEmpty() ? QDir::homePath() : request_.working_directory);

        const QStringList logged =
            redacted_command_args(launch_arguments, request_.sensitive_values);
        SPDLOG_INFO("running: {} {}", executable.toStdString(),
                    logged.join(QLatin1Char(' ')).toStdString());
        process_->start(executable, launch_arguments);
        if (request_.timeout_ms > 0)
            timeout_timer_->start(request_.timeout_ms);
        return true;
    }

    bool ProcessRunner::is_busy() const
    {
        return active_;
    }

    qint64 ProcessRunner::process_id() const
    {
        return process_->processId();
    }

    QProcess::ProcessState ProcessRunner::state() const
    {
        return process_->state();
    }

    command_result ProcessRunner::snapshot() const
    {
        return current_;
    }

    void ProcessRunner::cancel()
    {
        terminate(CommandOutcome::Cancelled);
    }

    void ProcessRunner::terminate(const CommandOutcome outcome)
    {
        if (!active_ || terminal_emitted_)
            return;
        forced_outcome_ = outcome;
        if (process_->state() == QProcess::Starting)
        {
            process_->kill();
            return;
        }
        force_kill_process_id_ = process_->processId();
        process_->terminate();
        force_kill_timer_->start(3000);
    }

    void ProcessRunner::handle_output()
    {
        const QByteArray bytes = process_->readAllStandardOutput();
        QString decoded = QString::fromUtf8(bytes);
        if (decoded.contains(QChar::ReplacementCharacter))
            decoded = QString::fromLocal8Bit(bytes);
        QString chunk = redact_sensitive_text(decoded, request_.sensitive_values);
        if (chunk.isEmpty())
            return;

        if (request_.output)
            request_.output(chunk);

        if (current_.output.size() + chunk.size() > k_max_captured_output)
        {
            const qsizetype keep = qMax<qsizetype>(0, k_max_captured_output - chunk.size());
            current_.output = keep > 0 ? current_.output.right(keep) : QString();
            if (chunk.size() > k_max_captured_output)
                chunk = chunk.right(k_max_captured_output);
            current_.output_truncated = true;
        }
        current_.output += chunk;

        int logged = 0;
        for (const QString& line : chunk.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
        {
            if (logged++ >= 100)
            {
                SPDLOG_DEBUG("[cmd] additional output omitted from this batch");
                break;
            }
            SPDLOG_DEBUG("[cmd] {}", line.left(4096).toStdString());
        }
    }

    void ProcessRunner::finish(const CommandOutcome outcome, const int exit_code,
                               const QProcess::ExitStatus exit_status, const QString& error_message)
    {
        if (!active_ || terminal_emitted_)
            return;

        terminal_emitted_ = true;
        timeout_timer_->stop();
        force_kill_timer_->stop();
        force_kill_process_id_ = -1;
        handle_output();

        current_.outcome = outcome;
        current_.exit_code = exit_code;
        current_.crashed = exit_status == QProcess::CrashExit || outcome == CommandOutcome::Crashed;
        current_.error_message = redact_sensitive_text(error_message, request_.sensitive_values);

        const Completion completion = std::move(completion_);
        const command_result completed = current_;
        completion_ = {};
        request_ = {};
        active_ = false;

        SPDLOG_INFO("command completed: {} (exit {})", command_outcome_name(outcome).toStdString(),
                    exit_code);
        if (completion)
            completion(completed);
    }
}
