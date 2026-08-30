#include "ui/InstallState.hpp"

#include <QFileInfo>
#include <QTimer>

#include "common/Log.hpp"
#include "common/GameVersion.hpp"
#include "network/CourierBridge.hpp"
#include "network/DownloadStatus.hpp"
#include "common/StatusBus.hpp"
#include "common/StatusReporter.hpp"
#include "runtime/PrefixInspector.hpp"
#include "runtime/RuntimeLocator.hpp"
#include "runtime/WineRegistry.hpp"
#include "config/Config.hpp"
#include <spdlog/spdlog.h>

namespace core::state
{
    namespace
    {
        const QString k_reporter_wine = QStringLiteral("wine");
        const QString k_reporter_auth = QStringLiteral("auth");
    }

    using core::network::CourierBridge;
    using core::network::DownloadStatus;
    using core::status::State;
    using core::status::Status;
    using core::status::StatusBus;
    using core::status::StatusReporter;

    InstallState::InstallState(QObject* parent) : QObject(parent)
    {
        probe_timer = new QTimer(this);
        probe_timer->setSingleShot(true);
        probe_timer->setInterval(0);
        connect(probe_timer, &QTimer::timeout, this, &InstallState::probe);

        connect(&StatusBus::instance(), &StatusBus::reporter_status_changed, this,
            [this](StatusReporter* reporter, const Status& status)
            {
                on_reporter_changed(reporter->reporter_name(), status);
            });
        connect(&CourierBridge::instance(), &CourierBridge::download_status,
                this, &InstallState::on_courier_status);
        connect(&util::config::Config::instance(), &util::config::Config::changed,
                this, &InstallState::schedule_probe, Qt::QueuedConnection);
    }

    InstallState::~InstallState()
    {
        cancel_update_check();
        if (update_checker)
        {
            courier_destroy(update_checker);
            update_checker = nullptr;
        }
    }

    QString InstallState::current_update_key() const
    {
        const auto& config = util::config::Config::instance();
        const auto& game = core::game::profile(config.game_version());
        return core::game::to_string(config.game_version())
            + QLatin1Char('|') + config.game_install_path()
            + QLatin1Char('|') + QString::fromLatin1(game.cdn_base_url);
    }

    void InstallState::cancel_update_check()
    {
        if (update_checker && update_operation_id != 0)
            courier_cancel(update_checker);
        CourierBridge::instance().clear_operation(update_operation_id);
        update_operation_id = 0;
        update_check_in_progress = false;
    }

    void InstallState::schedule_probe()
    {
        if (probe_timer && !probe_timer->isActive())
            probe_timer->start();
    }

    void InstallState::confirm_rules_reviewed()
    {
        if (rules_reviewed)
            return;
        rules_reviewed = true;
        recompute();
    }

    void InstallState::clear_rules_reviewed()
    {
        rules_reviewed = false;
    }

    void InstallState::probe()
    {
        if (probe_timer)
            probe_timer->stop();

        auto& config = util::config::Config::instance();
#if !defined(Q_OS_MACOS)



        if (core::wine::WineRegistry::identify(config.wine_binary())
            == core::wine::RuntimeType::Proton)
        {
            core::wine::repair_doubled_proton_prefix(config.proton_compat_data_root());
        }
#endif
        const QString prefix = config.prefix_root();
        prerequisites_confirmed = config.prerequisites_confirmed();
        rules_accepted = config.rules_accepted();
        runtime_chosen = config.runtime_selected();

#if defined(Q_OS_MACOS)
        const bool proton = false;
#else
        const bool proton = core::wine::WineRegistry::identify(config.wine_binary())
            == core::wine::RuntimeType::Proton;
#endif
        const QString runtimeIdentity = proton
            ? config.wine_binary()
            : core::wine::WineRegistry::resolve_wine_executable(config.wine_binary());
#if defined(Q_OS_MACOS)
        runtime_chosen = runtime_chosen
            && !runtimeIdentity.isEmpty()
            && QFileInfo(runtimeIdentity).isFile()
            && QFileInfo(runtimeIdentity).isExecutable();
#endif
        const auto inspection = core::wine::PrefixInspector::inspect(
            prefix,
            runtimeIdentity.isEmpty() ? config.wine_binary() : runtimeIdentity,
            proton);

        prefix_exists = inspection.exists;
        prefix_ready = inspection.exists && inspection.marker_valid
            && inspection.required_components_present(proton);
        game_installed = config.game_installed();
        authed = config.has_auth();
        probed = true;

        const QString key = current_update_key();
        if (key != checked_update_key)
        {
            cancel_update_check();
            if (update_checker)
            {
                courier_destroy(update_checker);
                update_checker = nullptr;
            }
            update_check_complete = false;
            update_needed = false;
            checked_update_key = key;
        }

        recompute();
        QTimer::singleShot(0, this, [this]() { start_update_check_if_needed(); });
    }

    void InstallState::start_update_check_if_needed()
    {
        if (!probed || !prerequisites_confirmed || !runtime_chosen || !prefix_ready || !game_installed
            || update_check_complete || update_check_in_progress
            || courier_working || wine_state == State::Working || auth_state == State::Working
            || !last_error.isEmpty())
        {
            return;
        }

        auto& config = util::config::Config::instance();
        const QString installPath = config.game_install_path();
        if (!config.path_inside_prefix(installPath))
        {
            update_check_complete = true;
            const QString warning = QStringLiteral(
                "The configured game folder is outside the active Wine prefix.");
            set_warning(warning);
            recompute();
            return;
        }
        const auto& game = core::game::profile(config.game_version());
        if (!update_checker)
        {
            update_checker = courier_create(
                game.cdn_base_url,
                CourierBridge::progress_callback(),
                CourierBridge::done_callback(),
                &CourierBridge::instance());
        }
        if (!update_checker)
        {
            update_check_complete = true;
            set_warning(QStringLiteral("Could not create the game update checker."));
            recompute();
            return;
        }

        update_check_in_progress = true;
        update_operation_id = courier_update_check(
            update_checker, installPath.toUtf8().constData());
        if (update_operation_id == 0)
        {
            update_check_in_progress = false;
            update_check_complete = true;
            set_warning(QStringLiteral("Could not start the game update check."));
            recompute();
            return;
        }
        CourierBridge::instance().begin_operation(update_operation_id);
        recompute();
    }

    void InstallState::on_courier_status(const DownloadStatus& status)
    {
        if (status.operation_id == update_operation_id)
        {
            if (status.base.state != State::Done && status.base.state != State::Failed)
                return;

            update_check_in_progress = false;
            update_check_complete = true;
            CourierBridge::instance().clear_operation(update_operation_id);
            update_operation_id = 0;

            if (status.base.state == State::Done)
            {
                update_needed = status.result == courier_result_update_available;
                if (!update_needed)
                    set_warning({});
            }
            else
            {
                update_needed = false;
                set_warning(QStringLiteral(
                    "The launcher could not check for game updates. You can still launch the installed version."));
            }
            recompute();
            return;
        }

        if (status.base.state == State::Working)
        {
            courier_working = true;
            set_warning({});
            recompute();
            return;
        }

        if (status.base.state == State::Done || status.base.state == State::Failed)
        {
            courier_working = false;
            if (status.base.state == State::Failed)
            {
                if (status.result == courier_result_cancelled)
                {
                    set_warning({});
                }
                else
                {
                    const QString detail = status.base.message.isEmpty()
                        ? QStringLiteral("The game download failed.")
                        : status.base.message;
                    set_warning(QStringLiteral(
                        "The last game transfer failed. Retry will verify existing files and continue: %1")
                        .arg(detail));
                }
            }
            else
            {
                set_warning({});
                update_check_complete = false;
                update_needed = false;
                schedule_probe();
            }
            recompute();
        }
    }

    void InstallState::set_error(const QString& message)
    {
        if (last_error == message)
            return;
        last_error = message;
        emit error_changed(last_error);
    }

    void InstallState::set_warning(const QString& message)
    {
        if (last_warning == message)
            return;
        last_warning = message;
        emit warning_changed(last_warning);
    }

    void InstallState::dismiss_error()
    {
        set_error({});
        probe();
    }


    void InstallState::on_reporter_changed(const QString& name, const Status& status)
    {
        if (name == k_reporter_wine)
        {
            wine_state = status.state;
            wine_phase = status.phase;
        }
        else if (name == k_reporter_auth)
        {
            auth_state = status.state;
        }
        else
        {
            return;
        }

        if (status.state == State::Failed)
        {
            set_error(status.message.isEmpty()
                ? QStringLiteral("The requested launcher operation failed.")
                : status.message);
            recompute();
            return;
        }
        if (status.state == State::Done)
        {
            set_error({});
            schedule_probe();
        }
        else
        {
            recompute();
        }
    }

    Stage InstallState::compute() const
    {
        if (!probed) return Stage::Probing;
        if (!last_error.isEmpty()) return Stage::Failed;
        if (!prerequisites_confirmed) return Stage::NeedsPrerequisites;
        if (!runtime_chosen) return Stage::NeedsRuntime;

        if (wine_state == State::Working)
        {
            if (wine_phase == QStringLiteral("game-running")
                || wine_phase == QStringLiteral("game-monitoring-uncertain"))
            {
                return Stage::Running;
            }
            if (wine_phase.startsWith(QStringLiteral("game-"))) return Stage::Launching;
            return Stage::SettingUpPrefix;
        }

        if (courier_working) return game_installed ? Stage::Updating : Stage::Downloading;
        if (!prefix_exists) return Stage::NeedsPrefix;
        if (!prefix_ready) return Stage::PrefixBroken;
        if (!game_installed) return Stage::NeedsDownload;
        if (update_check_in_progress) return Stage::CheckingUpdate;
        if (update_needed) return Stage::NeedsUpdate;
        if (!rules_accepted && !rules_reviewed) return Stage::NeedsRules;
        if (auth_state == State::Working) return Stage::Authenticating;
        if (!authed) return Stage::NeedsAuth;
        return Stage::Ready;
    }

    void InstallState::recompute()
    {
        const Stage next = compute();
        if (next == current)
            return;
        SPDLOG_INFO("install state: {} -> {}", to_string(current), to_string(next));
        current = next;
        emit stage_changed(current);
    }
}
