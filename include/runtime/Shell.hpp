#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <memory>

#include "common/GameVersion.hpp"
#include "common/StatusReporter.hpp"
#include "runtime/ProcessRunner.hpp"

namespace core::wine
{
    class GameSession;
    class PrefixSetupJob;
    class RuntimeLocator;

    class Shell : public status::StatusReporter
    {
        Q_OBJECT

    public:
        explicit Shell(QObject* parent = nullptr);
        ~Shell() override;

        void run_command(
            const QString& program, const QStringList& arguments,
            const QProcessEnvironment& environment = QProcessEnvironment::systemEnvironment());
        [[nodiscard]] bool is_wine_installed() const;
        [[nodiscard]] bool is_busy() const;
        [[nodiscard]] bool is_game_running() const;
        void cancel_current();
        void setup();
        void setup_wine();
        void setup_proton();
        void sync_dxvk();
        void run_game(const QString& user, const QString& token);
        void detect_existing_game();

    signals:
        void command_finished(const core::wine::command_result& result);
        void wine_setup_finished(bool ok);
        void setup_status(const QString& message);
        void user_error(const QString& title, const QString& message);
        void user_notice(const QString& message);
        void game_starting(core::game::GameVersion version);
        void game_started(core::game::GameVersion version);
        void game_exited(core::game::GameVersion version, int exit_code, bool crashed);

    private:
        void fail_user(const QString& title, const QString& message);
        [[nodiscard]] bool setup_entry_is_busy();

        std::unique_ptr<RuntimeLocator> runtime_;
        std::unique_ptr<ProcessRunner> runner_;
        std::unique_ptr<PrefixSetupJob> setup_job_;
        std::unique_ptr<GameSession> game_session_;
    };
}
