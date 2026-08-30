#include "runtime/Shell.hpp"

#include <utility>

#include "runtime/GameSession.hpp"
#include "runtime/PrefixSetupJob.hpp"
#include "runtime/RuntimeLocator.hpp"
#include "config/Config.hpp"
#include "i18n/LanguageManager.hpp"
#include <spdlog/spdlog.h>

namespace core::wine
{
    Shell::Shell(QObject* parent)
        : StatusReporter(QStringLiteral("wine"), parent),
          runtime_(std::make_unique<RuntimeLocator>()), runner_(std::make_unique<ProcessRunner>())
    {
        PrefixSetupJob::Callbacks setup_callbacks;
        setup_callbacks.command_finished = [this](const command_result& result)
        { emit command_finished(result); };
        setup_callbacks.setup_status = [this](const QString& message)
        { emit setup_status(message); };
        setup_callbacks.fail_user = [this](const QString& title, const QString& message)
        { fail_user(title, message); };
        setup_callbacks.user_error = [this](const QString& title, const QString& message)
        { emit user_error(title, message); };
        setup_callbacks.user_notice = [this](const QString& message) { emit user_notice(message); };
        setup_callbacks.setup_finished = [this](const bool ok) { emit wine_setup_finished(ok); };
        setup_callbacks.working =
            [this](const QString& phase, const double progress, const bool watchdog_exempt)
        { working(phase, progress, watchdog_exempt); };
        setup_callbacks.done = [this](const QString& message) { done(message); };
        setup_callbacks.failed = [this](const QString& message) { fail(message); };
        setup_job_ =
            std::make_unique<PrefixSetupJob>(*runtime_, *runner_, std::move(setup_callbacks));

        GameSession::Callbacks game_callbacks;
        game_callbacks.command_finished = [this](const command_result& result)
        { emit command_finished(result); };
        game_callbacks.fail_user = [this](const QString& title, const QString& message)
        { fail_user(title, message); };
        game_callbacks.user_notice = [this](const QString& message) { emit user_notice(message); };
        game_callbacks.game_starting = [this](const core::game::GameVersion version)
        { emit game_starting(version); };
        game_callbacks.game_started = [this](const core::game::GameVersion version)
        { emit game_started(version); };
        game_callbacks.game_exited =
            [this](const core::game::GameVersion version, const int exit_code, const bool crashed)
        { emit game_exited(version, exit_code, crashed); };
        game_callbacks.working =
            [this](const QString& phase, const double progress, const bool watchdog_exempt)
        { working(phase, progress, watchdog_exempt); };
        game_callbacks.done = [this](const QString& message) { done(message); };
        game_callbacks.failed = [this](const QString& message) { fail(message); };
        game_session_ =
            std::make_unique<GameSession>(*runtime_, *runner_, std::move(game_callbacks));
    }

    Shell::~Shell() = default;

    bool Shell::is_busy() const
    {
        return runner_->is_busy() || setup_job_->is_active() || game_session_->is_busy();
    }

    bool Shell::is_game_running() const
    {
        return game_session_->is_game_running();
    }

    bool Shell::is_wine_installed() const
    {
        return runtime_->is_wine_installed();
    }

    void Shell::run_command(const QString& program, const QStringList& arguments,
                            const QProcessEnvironment& environment)
    {
        if (is_busy())
        {
            fail_user(QStringLiteral("Could Not Start Command"),
                      QStringLiteral("The selected executable could not be "
                                     "started."));
            return;
        }

        ProcessRunner::Request request;
        request.program = program;
        request.arguments = arguments;
        request.environment = environment;
        request.timeout_ms = 15 * 60 * 1000;
        request.sensitive_values = {util::config::Config::instance().token()};
        if (!runner_->start(std::move(request), [this](const command_result& result)
                            { emit command_finished(result); }))
        {
            fail_user(QStringLiteral("Could Not Start Command"),
                      QStringLiteral("The selected executable could not be "
                                     "started."));
        }
    }

    void Shell::cancel_current()
    {
        if (!is_busy())
            return;
        if (game_session_->is_busy())
        {
            game_session_->cancel();
            return;
        }
        if (setup_job_->is_active())
        {
            setup_job_->cancel();
            return;
        }
        runner_->cancel();
    }

    bool Shell::setup_entry_is_busy()
    {
        if (!is_busy())
            return false;
#if defined(Q_OS_MACOS)
        const QString busy_message = QStringLiteral("Another runtime or game process is already "
                                                    "running.");
#else
        const QString busy_message = QStringLiteral("Another Wine or game process is already "
                                                    "running.");
#endif
        fail_user(QStringLiteral("Launcher Busy"), busy_message);
        emit wine_setup_finished(false);
        return true;
    }

    void Shell::setup()
    {
        if (!setup_entry_is_busy())
            setup_job_->setup();
    }

    void Shell::setup_wine()
    {
        if (!setup_entry_is_busy())
            setup_job_->setup_wine();
    }

    void Shell::setup_proton()
    {
        if (!setup_entry_is_busy())
            setup_job_->setup_proton();
    }

    void Shell::sync_dxvk()
    {
        if (!setup_entry_is_busy())
            setup_job_->sync_dxvk();
    }

    void Shell::run_game(const QString& user, const QString& token)
    {
        if (is_busy())
        {
            fail_user(QStringLiteral("Launcher Busy"),
                      is_game_running() ? QStringLiteral("Alicia is already running.")
                                        : QStringLiteral("Wait for the current runtime "
                                                         "operation to finish."));
            return;
        }
        game_session_->run_game(user, token);
    }

    void Shell::detect_existing_game()
    {
        if (runner_->is_busy() || setup_job_->is_active())
        {
            return;
        }
        game_session_->detect_existing_game();
    }

    void Shell::fail_user(const QString& title, const QString& message)
    {
        SPDLOG_ERROR("{}: {}", title.toStdString(), message.toStdString());
        const QString translated_title = util::i18n::translate(title);
        const QString translated_message = util::i18n::translate(message);
        fail(translated_message);
        emit user_error(translated_title, translated_message);
    }
}
