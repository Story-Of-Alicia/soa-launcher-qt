#include "core/wine/GameSession.hpp"

#include "core/wine/AliciaLogHook.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#include <memory>
#include <utility>

#include "core/game/GameVersion.hpp"
#include "core/wine/PrefixInspector.hpp"
#include "core/wine/RuntimeLocator.hpp"
#include "core/wine/WineRegistry.hpp"
#include "util/Config.hpp"
#include "util/LaunchArguments.hpp"
#include "util/LanguageManager.hpp"
#include <spdlog/spdlog.h>

namespace core::wine
{
    using util::config::Config;

    namespace
    {

        constexpr const char* k_default_virtual_desktop = "1280x720";
        constexpr int k_registry_timeout_ms = 45 * 1000;
        constexpr int k_probe_timeout_ms = 5 * 1000;
#if defined(Q_OS_MACOS)
        constexpr int k_initial_probe_delay_ms = 8000;
        constexpr int k_start_probe_interval_ms = 3000;
        constexpr int k_running_probe_interval_ms = 10000;
#else
        constexpr int k_initial_probe_delay_ms = 100;
        constexpr int k_start_probe_interval_ms = 350;
        constexpr int k_running_probe_interval_ms = 2000;
#endif
        constexpr int k_start_max_attempts = 60;
        constexpr int k_wrapper_exit_grace_ms = 8000;
        constexpr int k_absolute_start_timeout_ms = 120 * 1000;
        constexpr int k_probe_failure_limit = 4;
        constexpr int k_attached_probe_failure_limit = 8;
        constexpr int k_missing_confirmation_limit = 3;
#if defined(Q_OS_LINUX)
        constexpr bool k_primary_probe_is_host = true;
#else
        constexpr bool k_primary_probe_is_host = false;
#endif

#if defined(Q_OS_MACOS)
        bool patch_existing_alice_config(const QString& game_directory, QString& message)
        {
            const QString config_path = QDir(game_directory).filePath(QStringLiteral("alice.cfg"));
            QFile source(config_path);
            if (!source.exists())
            {
                message = QStringLiteral("alice.cfg does not exist yet. This launch "
                                         "will still use the safe-display profile; "
                                         "select Low Graphics again after the game "
                                         "creates the file.");
                return true;
            }
            if (!source.open(QIODevice::ReadOnly))
            {
                message = QStringLiteral("The existing alice.cfg could not be read.");
                return false;
            }

            const QByteArray original = source.readAll();
            source.close();
            QString text = QString::fromLatin1(original);
            bool changed = false;
            const QMap<QString, QString> replacements {
                {QStringLiteral("r_motionBlur"), QStringLiteral("0")},
                {QStringLiteral("r_mrt"), QStringLiteral("0")},
                {QStringLiteral("r_radialBlur"), QStringLiteral("0")},
                {QStringLiteral("cmd_force_simpleShadow"), QStringLiteral("1")}};
            for (auto it = replacements.cbegin(); it != replacements.cend(); ++it)
            {
                const QRegularExpression expression(
                    QStringLiteral(R"((?m)^(\s*seta\s+%1\s+")[^"]*("))")
                        .arg(QRegularExpression::escape(it.key())));
                const QRegularExpressionMatch match = expression.match(text);
                if (!match.hasMatch())
                    continue;
                const QString replacement = match.captured(1) + it.value() + match.captured(2);
                text.replace(match.capturedStart(), match.capturedLength(), replacement);
                changed = true;
            }

            if (!changed)
            {
                message = QStringLiteral("alice.cfg was found, but none of the "
                                         "verified compatibility keys were present.");
                return true;
            }

            const QString backup_path = config_path + QStringLiteral(".soa-macos-backup");
            if (!QFileInfo::exists(backup_path))
            {
                QSaveFile backup(backup_path);
                if (!backup.open(QIODevice::WriteOnly) ||
                    backup.write(original) != original.size() || !backup.commit())
                {
                    message = QStringLiteral("The launcher could not create an "
                                             "alice.cfg backup.");
                    return false;
                }
            }

            const QByteArray updated = text.toLatin1();
            QSaveFile output(config_path);
            if (!output.open(QIODevice::WriteOnly) || output.write(updated) != updated.size() ||
                !output.commit())
            {
                message = QStringLiteral("The launcher could not apply the "
                                         "conservative alice.cfg profile.");
                return false;
            }
            message = QStringLiteral("Applied the conservative macOS graphics "
                                     "profile and saved "
                                     "alice.cfg.soa-macos-backup.");
            return true;
        }
#endif
    }

    GameSession::GameSession(RuntimeLocator& runtime, ProcessRunner& runner, Callbacks callbacks,
                             QObject* parent)
        : QObject(parent), runtime_(runtime), runner_(runner), callbacks_(std::move(callbacks)),
          diagnostics_(nullptr)
    {
        probe_process_ = new QProcess(this);
        probe_process_->setProcessChannelMode(QProcess::MergedChannels);
        monitor_timer_ = new QTimer(this);
        monitor_timer_->setSingleShot(true);
        probe_timeout_timer_ = new QTimer(this);
        probe_timeout_timer_->setSingleShot(true);

        connect(probe_process_, &QProcess::readyReadStandardOutput, this,
                [this]() { handle_game_probe_output(); });
        connect(probe_process_, &QProcess::errorOccurred, this,
                [this](const QProcess::ProcessError error)
                {
                    if (error != QProcess::FailedToStart || !probe_completion_)
                    {
                        return;
                    }
                    const auto completion = std::move(probe_completion_);
                    probe_completion_ = {};
                    probe_timeout_timer_->stop();
                    const bool host_probe = probe_host_mode_;
                    probe_output_.clear();
                    probe_host_mode_ = false;
                    if (host_probe)
                    {
                        SPDLOG_WARN("Alicia host process probe failed to "
                                    "start: {}",
                                    probe_process_->errorString().toStdString());
                    }
                    else
                    {
                        SPDLOG_DEBUG("Windows-side Alicia process probe failed "
                                     "to start; the host probe will be used: {}",
                                     probe_process_->errorString().toStdString());
                    }
                    completion(false, std::nullopt);
                });
        connect(probe_process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this](const int exit_code, const QProcess::ExitStatus exit_status)
                {
                    handle_game_probe_output();
                    if (!probe_completion_)
                        return;

                    const auto completion = std::move(probe_completion_);
                    probe_completion_ = {};
                    probe_timeout_timer_->stop();
                    const bool host_probe = probe_host_mode_;
                    const QString output = host_probe
                                               ? QString::fromUtf8(probe_output_)
                                               : decode_windows_process_output(probe_output_);
                    probe_output_.clear();
                    probe_host_mode_ = false;
                    const bool query_ok = exit_status == QProcess::NormalExit && exit_code == 0;
                    const auto info =
                        host_probe ? find_host_process(output, QStringLiteral("Alicia.exe"))
                                   : find_windows_process(output, QStringLiteral("Alicia.exe"));
                    if (!query_ok)
                    {
                        if (host_probe)
                        {
                            SPDLOG_WARN("Alicia host process probe failed "
                                        "(exit {}): {}",
                                        exit_code, output.left(2048).toStdString());
                        }
                        else
                        {
                            SPDLOG_DEBUG("Windows-side Alicia process probe "
                                         "failed (exit {}); the host probe will "
                                         "be used: {}",
                                         exit_code, output.left(2048).toStdString());
                        }
                    }
                    completion(query_ok, info);
                });
        connect(monitor_timer_, &QTimer::timeout, this, [this]() { poll_game_process(); });
        connect(probe_timeout_timer_, &QTimer::timeout, this,
                [this]()
                {
                    if (!probe_completion_)
                        return;
                    const auto completion = std::move(probe_completion_);
                    probe_completion_ = {};
                    const bool host_probe = probe_host_mode_;
                    probe_output_.clear();
                    probe_host_mode_ = false;
                    if (host_probe)
                    {
                        SPDLOG_WARN("Alicia host process probe timed out");
                    }
                    else
                    {
                        SPDLOG_DEBUG("Windows-side Alicia process probe timed "
                                     "out; the host probe will be used");
                    }
                    probe_process_->kill();
                    completion(false, std::nullopt);
                });
    }

    GameSession::~GameSession()
    {
        stop_probe();
        diagnostics_.reset();
    }

    GamePhase GameSession::phase() const
    {
        return phase_;
    }

    bool GameSession::is_busy() const
    {
        return phase_ != GamePhase::Idle || bool(probe_completion_) ||
               probe_process_->state() != QProcess::NotRunning;
    }

    bool GameSession::is_game_running() const
    {
        return phase_ == GamePhase::Running || phase_ == GamePhase::MonitoringUncertain;
    }

    bool GameSession::transition(const GamePhase next)
    {
        if (phase_ == next)
            return true;
        if (!is_valid_game_transition(phase_, next))
        {
            SPDLOG_ERROR("invalid Alicia lifecycle transition {} -> {}",
                         game_phase_name(phase_).toStdString(),
                         game_phase_name(next).toStdString());
            return false;
        }
        SPDLOG_INFO("Alicia lifecycle: {} -> {}", game_phase_name(phase_).toStdString(),
                    game_phase_name(next).toStdString());
        phase_ = next;
        if (next == GamePhase::Launching)
        {
            verification_attempts_ = 0;
            probe_failures_ = 0;
            missing_probes_ = 0;
            wrapper_state_ = WrapperState::NotStarted;
            wrapper_result_ = {};
            wrapper_finished_at_ms_ = -1;
            origin_ = SessionOrigin::Started;
            tracked_game_process_ = {};
            tracked_host_process_ = {};
            diagnostic_sample_armed_ = false;
        }
        else if (next == GamePhase::Running)
        {
            probe_failures_ = 0;
            missing_probes_ = 0;
        }
        return true;
    }

    void GameSession::stop_probe()
    {
        monitor_timer_->stop();
        probe_timeout_timer_->stop();
        probe_completion_ = {};
        probe_output_.clear();
        probe_host_mode_ = false;
        if (probe_process_->state() != QProcess::NotRunning)
        {
            probe_process_->kill();
        }
    }

    void GameSession::clear_session_state()
    {
        stop_probe();
        ++generation_;
        launch_.reset();
        if (!pending_registry_file_.isEmpty())
            QFile::remove(pending_registry_file_);
        pending_registry_file_.clear();
        clear_game_probe_context();
        tracked_game_process_ = {};
        tracked_host_process_ = {};
        wrapper_state_ = WrapperState::NotStarted;
        last_stage_result_ = {};
        wrapper_result_ = {};
        wrapper_finished_at_ms_ = -1;
        origin_ = SessionOrigin::Started;
        verification_attempts_ = 0;
        probe_failures_ = 0;
        missing_probes_ = 0;
        diagnostic_sample_armed_ = false;
    }

    void GameSession::run_game(const QString& user, const QString& token)
    {
        if (is_busy() || runner_.is_busy())
        {
            if (callbacks_.fail_user)
            {
                callbacks_.fail_user(QStringLiteral("Launcher Busy"),
                                     is_game_running()
                                         ? QStringLiteral("Alicia is already running.")
                                         : QStringLiteral("Wait for the current runtime "
                                                          "operation to finish."));
            }
            return;
        }
        if (!runtime_.is_wine_installed())
        {
            if (callbacks_.fail_user)
            {
                callbacks_.fail_user(QStringLiteral("Runtime Missing"),
                                     QStringLiteral("The selected runtime is unavailable."));
            }
            return;
        }
        if ((user.isEmpty() || token.isEmpty())
            && !util::launch_arguments::developer_mode_enabled())
        {
            if (callbacks_.fail_user)
            {
                callbacks_.fail_user(QStringLiteral("Sign In Required"),
                                     QStringLiteral("Your login session is missing or "
                                                    "expired. Sign in again."));
            }
            return;
        }

        const auto version = Config::instance().game_version();
        const auto& profile = core::game::profile(version);
        const QString game_directory = Config::instance().game_install_path();
        const QString executable =
            QDir(game_directory).filePath(QString::fromLatin1(profile.executable_name));
        if (!QFileInfo(executable).isFile())
        {
            if (callbacks_.fail_user)
            {
                callbacks_.fail_user(QStringLiteral("Game Not Found"),
                                     QStringLiteral("%1 was not found in the selected "
                                                    "game folder.")
                                         .arg(QString::fromLatin1(profile.executable_name)));
            }
            return;
        }
        if (!Config::instance().path_inside_prefix(game_directory))
        {
            if (callbacks_.fail_user)
            {
                callbacks_.fail_user(QStringLiteral("Invalid Game Path"),
                                     QStringLiteral("The configured game folder is "
                                                    "outside the active compatibility "
                                                    "prefix."));
            }
            return;
        }

        PendingLaunch launch;
        launch.version = version;
        launch.user = user;
        launch.token = token;
        launch.game_directory = game_directory;
        launch.executable_path = executable;

        diagnostics_.reset();
        capture_game_probe_context();
        if (!probe_context_valid_)
        {
            if (callbacks_.fail_user)
            {
                callbacks_.fail_user(QStringLiteral("Runtime Not Available"),
                                     QStringLiteral("The launcher could not prepare the "
                                                    "Alicia process monitor for the "
                                                    "selected runtime."));
            }
            return;
        }
        ++generation_;
        begin_preflight(std::move(launch));
    }

    void GameSession::begin_preflight(PendingLaunch launch)
    {
        launch_ = std::move(launch);
        tracked_version_ = launch_->version;
        origin_ = SessionOrigin::Started;
        if (!transition(GamePhase::Preflight))
            return;
        if (callbacks_.game_starting)
            callbacks_.game_starting(launch_->version);
        if (callbacks_.working)
        {
            callbacks_.working(QStringLiteral("game-preflight"), -1.0, true);
        }

        const quint64 generation = generation_;
        if (!start_game_lifecycle_probe(
                [this, generation](const bool ok, const std::optional<WindowsProcessInfo> info)
                {
                    if (generation != generation_ || phase_ != GamePhase::Preflight)
                    {
                        return;
                    }
                    if (!ok)
                    {
                        fail_game_launch(QStringLiteral("The launcher could not verify whether "
                                                        "Alicia.exe is already running. This "
                                                        "safety check prevents terminating an "
                                                        "active game session."));
                        return;
                    }
                    if (info)
                    {
                        attach_to_game_process(*info, SessionOrigin::Restored);
                        return;
                    }
                    clean_stale_prefix_processes();
                }))
        {
            fail_game_launch(QStringLiteral("The Alicia process safety check could not "
                                            "be started."));
        }
    }

    void GameSession::clean_stale_prefix_processes()
    {
        if (phase_ != GamePhase::Preflight)
            return;
        const QString server = runtime_.wineserver_binary();
        if (server.isEmpty() || !QFileInfo(server).isExecutable())
        {
            SPDLOG_WARN("wineserver was not found; stale-prefix "
                        "cleanup is unavailable");
            if (callbacks_.user_notice)
            {
                callbacks_.user_notice(QStringLiteral("The selected runtime does not expose its "
                                                      "process-control helper, so stale prefix "
                                                      "processes could not be cleaned before "
                                                      "launch."));
            }
            begin_first_launch_setup();
            return;
        }

        if (!transition(GamePhase::CleaningPrefix))
            return;
        const bool proton = runtime_.runtime_is_proton();
        QProcessEnvironment environment =
            proton ? runtime_.umu_environment() : QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("WINEPREFIX"), Config::instance().prefix_root());
        environment.insert(QStringLiteral("WINEDEBUG"), QStringLiteral("-all"));
#if defined(Q_OS_MACOS)
        environment.insert(QStringLiteral("WINEARCH"), QStringLiteral("win64"));
#endif
        if (!proton)
            runtime_.apply_wine_environment(environment);

        if (callbacks_.working)
        {
            callbacks_.working(QStringLiteral("game-prefix-cleanup"), -1.0, true);
        }
        SPDLOG_INFO("cleaning stale Wine processes for prefix {}",
                    Config::instance().prefix_root().toStdString());

        ProcessRunner::Request request;
        request.program = server;
        request.arguments = {QStringLiteral("-k")};
        request.environment = environment;
        request.timeout_ms = 15 * 1000;
        if (!run_stage_command(std::move(request), GamePhase::CleaningPrefix,
                               [this](const command_result& result)
                               {
                                   if (result.outcome == CommandOutcome::FailedToStart ||
                                       result.outcome == CommandOutcome::TimedOut ||
                                       result.outcome == CommandOutcome::Crashed ||
                                       result.outcome == CommandOutcome::Cancelled)
                                   {
                                       fail_game_launch(
                                           QStringLiteral("The launcher could not stop stale "
                                                          "runtime processes in the game prefix."));
                                       return;
                                   }
                                   wait_for_prefix_shutdown();
                               }))
        {
            fail_game_launch(QStringLiteral("The launcher could not start stale "
                                            "runtime-process cleanup."));
        }
    }

    void GameSession::wait_for_prefix_shutdown()
    {
        if (phase_ != GamePhase::CleaningPrefix)
            return;
        const QString server = runtime_.wineserver_binary();
        const bool proton = runtime_.runtime_is_proton();
        QProcessEnvironment environment =
            proton ? runtime_.umu_environment() : QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("WINEPREFIX"), Config::instance().prefix_root());
        environment.insert(QStringLiteral("WINEDEBUG"), QStringLiteral("-all"));
#if defined(Q_OS_MACOS)
        environment.insert(QStringLiteral("WINEARCH"), QStringLiteral("win64"));
#endif
        if (!proton)
            runtime_.apply_wine_environment(environment);

        ProcessRunner::Request request;
        request.program = server;
        request.arguments = {QStringLiteral("-w")};
        request.environment = environment;
        request.timeout_ms = 15 * 1000;
        if (!run_stage_command(std::move(request), GamePhase::CleaningPrefix,
                               [this](const command_result& result)
                               {
                                   if (result.outcome == CommandOutcome::FailedToStart ||
                                       result.outcome == CommandOutcome::TimedOut ||
                                       result.outcome == CommandOutcome::Crashed ||
                                       result.outcome == CommandOutcome::Cancelled)
                                   {
                                       fail_game_launch(QStringLiteral(
                                           "The runtime did not finish shutting down "
                                           "stale prefix processes."));
                                       return;
                                   }
                                   SPDLOG_INFO("stale Wine prefix processes cleaned "
                                               "successfully");
                                   begin_first_launch_setup();
                               }))
        {
            fail_game_launch(QStringLiteral("The launcher could not wait for stale "
                                            "runtime processes to stop."));
        }
    }

    bool GameSession::run_stage_command(ProcessRunner::Request request,
                                        const GamePhase expected_phase,
                                        std::function<void(const command_result&)> completion)
    {
        const quint64 generation = generation_;
        return runner_.start(std::move(request),
                             [this, generation, expected_phase,
                              completion = std::move(completion)](const command_result& result)
                             {
                                 if (generation != generation_ || phase_ != expected_phase)
                                 {
                                     return;
                                 }
                                 last_stage_result_ = result;
                                 if (callbacks_.command_finished)
                                     callbacks_.command_finished(result);
                                 if (completion)
                                     completion(result);
                             });
    }

    void GameSession::begin_first_launch_setup()
    {
        if (!launch_ || (phase_ != GamePhase::Preflight && phase_ != GamePhase::CleaningPrefix))
        {
            return;
        }
        if (!transition(GamePhase::FirstLaunchSetup))
            return;
        if (callbacks_.working)
        {
            callbacks_.working(QStringLiteral("game-first-launch"), -1.0, false);
        }

        const bool proton = runtime_.runtime_is_proton();
        QProcessEnvironment environment =
            proton ? runtime_.umu_environment() : QProcessEnvironment::systemEnvironment();
        QString program;
        if (proton)
        {
            program = umu_path();
            if (program.isEmpty() || !QFileInfo(program).isExecutable())
            {
                fail_game_launch(QStringLiteral("Proton launch requires a working umu-run installation."));
                return;
            }
            environment.insert(QStringLiteral("PROTON_VERB"), QStringLiteral("runinprefix"));
        }
        else
        {
            program = runtime_.wine_binary();
            environment.insert(QStringLiteral("WINEPREFIX"), Config::instance().prefix_root());
            runtime_.apply_wine_environment(environment);
        }

        QStringList arguments;
        arguments << QStringLiteral("reg.exe") << QStringLiteral("query")
                  << QStringLiteral("HKCU\\Software\\Story of Alicia\\Launcher");

        ProcessRunner::Request request;
        request.program = program;
        request.arguments = arguments;
        request.environment = environment;
        request.timeout_ms = k_registry_timeout_ms;
        request.sensitive_values = {launch_->token};
        if (!run_stage_command(std::move(request), GamePhase::FirstLaunchSetup,
                               [this](const command_result& result)
                               { handle_first_launch_query(result); }))
        {
            fail_game_launch(QStringLiteral("First-launch setup could not inspect the "
                                            "compatibility registry."));
        }
    }

    bool GameSession::write_first_launch_registry(const PendingLaunch& launch,
                                                  const QString& query_output,
                                                  QString& registry_file, bool& needs_import) const
    {
        const auto& profile = core::game::profile(launch.version);
        const QString base_flag = QString::fromLatin1(profile.first_launch_registry_value);
        const QString audio_flag = QString::fromLatin1(profile.audio_settings_setup_registry_value);
        const bool query_succeeded = !query_output.isNull();
        const bool base_done =
            query_succeeded && query_output.contains(base_flag, Qt::CaseInsensitive);
        const bool audio_done =
            audio_flag.isEmpty() ||
            (query_succeeded && query_output.contains(audio_flag, Qt::CaseInsensitive));
#if defined(Q_OS_MACOS)
        const QString macos_profile = Config::instance().macos_compatibility_profile();
        const bool macos_profile_done =
            query_succeeded &&
            query_output.contains(QStringLiteral("MacCompatibilityProfile"), Qt::CaseInsensitive) &&
            query_output.contains(macos_profile, Qt::CaseInsensitive);
#else
        const bool macos_profile_done = true;
#endif
        needs_import = !base_done || !audio_done || !macos_profile_done;
#if defined(Q_OS_MACOS)
        needs_import = true;
#endif
        if (!needs_import)
            return true;

        QString registry = QStringLiteral("REGEDIT4\r\n\r\n");
#if !defined(Q_OS_MACOS)
        if (!base_done && profile.video_settings_registry_key[0] != '\0')
        {
            registry += QStringLiteral("[HKEY_CURRENT_USER\\%1]\r\n")
                            .arg(QString::fromLatin1(profile.video_settings_registry_key));
            registry += QStringLiteral("\"screenResolutionID\"=\"0\"\r\n");
            registry += QStringLiteral("\"screenWindowType\"=\"1\"\r\n");
            registry += QStringLiteral("\"Width\"=\"0\"\r\n");
            registry += QStringLiteral("\"Height\"=\"0\"\r\n\r\n");
        }
#else
        registry += QStringLiteral("[HKEY_CURRENT_USER\\Software\\Wine\\"
                                   "Direct3D]\r\n");
        registry += QStringLiteral("\"renderer\"=-\r\n\r\n");
        registry += QStringLiteral("[HKEY_CURRENT_USER\\Software\\Wine\\"
                                   "Mac Driver]\r\n");
        registry += QStringLiteral("\"RetinaMode\"=\"n\"\r\n\r\n");
        registry += QStringLiteral("[HKEY_CURRENT_USER\\Software\\Wine\\"
                                   "AppDefaults\\Alicia.exe\\Mac Driver]\r\n");
        if (MacLaunchDiagnostics::profile_uses_virtual_desktop(macos_profile))
        {
            registry += QStringLiteral("\"CaptureDisplaysForFullscreen\"=\"n\"\r\n");
            registry += QStringLiteral("\"AllowSetGamma\"=\"n\"\r\n");
        }
        else
        {
            registry += QStringLiteral("\"CaptureDisplaysForFullscreen\"=-\r\n");
            registry += QStringLiteral("\"AllowSetGamma\"=-\r\n");
        }
        const QString surface_mode = MacLaunchDiagnostics::opengl_surface_mode(macos_profile);
        if (surface_mode.isEmpty())
        {
            registry += QStringLiteral("\"OpenGLSurfaceMode\"=-\r\n\r\n");
        }
        else
        {
            registry += QStringLiteral("\"OpenGLSurfaceMode\"=\"%1\"\r\n\r\n").arg(surface_mode);
        }
#endif
        if (!audio_done && profile.audio_settings_registry_key[0] != '\0')
        {
            registry += QStringLiteral("[HKEY_CURRENT_USER\\%1]\r\n")
                            .arg(QString::fromLatin1(profile.audio_settings_registry_key));
            registry += QStringLiteral("\"VolBGM\"=\"30\"\r\n");
            registry += QStringLiteral("\"VolSFX\"=\"30\"\r\n\r\n");
        }
        registry += QStringLiteral("[HKEY_CURRENT_USER\\Software\\"
                                   "Story of Alicia\\Launcher]\r\n");
        if (!base_done)
        {
            registry += QStringLiteral("\"%1\"=dword:00000001\r\n").arg(base_flag);
        }
        if (!audio_done && !audio_flag.isEmpty())
        {
            registry += QStringLiteral("\"%1\"=dword:00000001\r\n").arg(audio_flag);
        }
#if defined(Q_OS_MACOS)
        registry += QStringLiteral("\"MacCompatibilityProfile\"=\"%1\"\r\n").arg(macos_profile);
#endif

        registry_file = QDir(Config::instance().prefix_root())
                            .filePath(QStringLiteral("drive_c/.soa-first-launch.reg"));
        QSaveFile file(registry_file);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            return false;
        }
        if (file.write(registry.toLatin1()) < 0)
            return false;
        return file.commit();
    }

    void GameSession::handle_first_launch_query(const command_result& result)
    {
        if (phase_ != GamePhase::FirstLaunchSetup || !launch_)
        {
            return;
        }
        if (!result.started || result.outcome == CommandOutcome::TimedOut ||
            result.outcome == CommandOutcome::Cancelled ||
            result.outcome == CommandOutcome::FailedToStart ||
            result.outcome == CommandOutcome::Crashed)
        {
            fail_game_launch(QStringLiteral("First-launch setup could not inspect the "
                                            "compatibility registry."));
            return;
        }

        bool needs_import = false;
        QString registry_file;
        const QString query_output = result.exit_code == 0 ? result.output : QString();
        if (!write_first_launch_registry(*launch_, query_output, registry_file, needs_import))
        {
            fail_game_launch(QStringLiteral("First-launch setup could not prepare the "
                                            "default game settings."));
            return;
        }
        if (!needs_import)
        {
            launch_pending_game();
            return;
        }

        pending_registry_file_ = registry_file;
        const bool proton = runtime_.runtime_is_proton();
        QProcessEnvironment environment =
            proton ? runtime_.umu_environment() : QProcessEnvironment::systemEnvironment();
        QString program;
        if (proton)
        {
            program = umu_path();
            if (program.isEmpty() || !QFileInfo(program).isExecutable())
            {
                QFile::remove(pending_registry_file_);
                pending_registry_file_.clear();
                fail_game_launch(QStringLiteral("Proton launch requires a working umu-run installation."));
                return;
            }
            environment.insert(QStringLiteral("PROTON_VERB"), QStringLiteral("runinprefix"));
        }
        else
        {
            program = runtime_.wine_binary();
            environment.insert(QStringLiteral("WINEPREFIX"), Config::instance().prefix_root());
            runtime_.apply_wine_environment(environment);
        }

        QStringList arguments;
        arguments << QStringLiteral("regedit.exe") << QStringLiteral("/S")
                  << QStringLiteral("C:\\.soa-first-launch.reg");

        ProcessRunner::Request request;
        request.program = program;
        request.arguments = arguments;
        request.environment = environment;
        request.timeout_ms = k_registry_timeout_ms;
        request.sensitive_values = {launch_->token};
        if (!run_stage_command(std::move(request), GamePhase::FirstLaunchSetup,
                               [this](const command_result& import_result)
                               {
                                   QFile::remove(pending_registry_file_);
                                   pending_registry_file_.clear();
                                   if (!import_result.ok())
                                   {
                                       fail_game_launch(
                                           QStringLiteral("First-launch setup could not apply the "
                                                          "default video and audio settings."));
                                       return;
                                   }
                                   launch_pending_game();
                               }))
        {
            QFile::remove(pending_registry_file_);
            pending_registry_file_.clear();
            fail_game_launch(QStringLiteral("First-launch setup could not start the "
                                            "compatibility-registry import."));
        }
    }

    void GameSession::launch_pending_game()
    {
        if (phase_ != GamePhase::FirstLaunchSetup || !launch_)
        {
            return;
        }
        if (!transition(GamePhase::Launching))
            return;
        launch_game_process();
    }

    bool GameSession::start_game_probe(
        std::function<void(bool, std::optional<WindowsProcessInfo>)> completion)
    {
        if (!completion || probe_completion_ || probe_process_->state() != QProcess::NotRunning)
        {
            return false;
        }

        const bool proton = probe_context_valid_ ? false : runtime_.runtime_is_proton();
        const QString program = probe_context_valid_ ? probe_program_snapshot_
                                                     : (proton ? runtime_.proton_wine_binary()
                                                               : runtime_.wine_binary());
        if (program.isEmpty())
            return false;

        QProcessEnvironment environment =
            probe_context_valid_
                ? probe_environment_snapshot_
                : (proton ? runtime_.umu_environment() : QProcessEnvironment::systemEnvironment());
        environment.insert(QStringLiteral("WINEPREFIX"), probe_context_valid_
                                                             ? probe_prefix_snapshot_
                                                             : Config::instance().prefix_root());
        environment.insert(QStringLiteral("WINEDEBUG"), QStringLiteral("-all"));
#if defined(Q_OS_MACOS)
        environment.insert(QStringLiteral("WINEARCH"), QStringLiteral("win64"));
#endif
        if (!probe_context_valid_ && !proton)
            runtime_.apply_wine_environment(environment);

        QString executable;
        QStringList arguments;
        QProcessEnvironment prepared_environment;
        const QStringList wine_arguments {QStringLiteral("tasklist.exe"), QStringLiteral("/FO"),
                                          QStringLiteral("CSV"), QStringLiteral("/NH")};
        if (!ProcessRunner::prepare_host_invocation(program, wine_arguments, environment,
                                                    executable, arguments, prepared_environment))
        {
            return false;
        }

        probe_output_.clear();
        probe_host_mode_ = false;
        probe_completion_ = std::move(completion);
        probe_process_->setProcessEnvironment(prepared_environment);
        probe_process_->setWorkingDirectory(QDir::homePath());
        probe_process_->start(executable, arguments);
        probe_timeout_timer_->start(k_probe_timeout_ms);
        return true;
    }

    void GameSession::capture_game_probe_context()
    {
        const bool proton = runtime_.runtime_is_proton();
        probe_program_snapshot_ = proton ? runtime_.proton_wine_binary() : runtime_.wine_binary();
        probe_prefix_snapshot_ = Config::instance().prefix_root();
        probe_environment_snapshot_ =
            proton ? runtime_.umu_environment() : QProcessEnvironment::systemEnvironment();
        probe_environment_snapshot_.insert(QStringLiteral("WINEPREFIX"), probe_prefix_snapshot_);
        probe_environment_snapshot_.insert(QStringLiteral("WINEDEBUG"), QStringLiteral("-all"));
#if defined(Q_OS_MACOS)
        probe_environment_snapshot_.insert(QStringLiteral("WINEARCH"), QStringLiteral("win64"));
#endif
        if (!proton)
        {
            runtime_.apply_wine_environment(probe_environment_snapshot_);
        }
        probe_context_valid_ =
            !probe_program_snapshot_.isEmpty() && !probe_prefix_snapshot_.isEmpty();
    }

    void GameSession::clear_game_probe_context()
    {
        probe_context_valid_ = false;
        probe_program_snapshot_.clear();
        probe_prefix_snapshot_.clear();
        probe_environment_snapshot_ = {};
    }

    bool GameSession::start_host_game_probe(
        std::function<void(bool, std::optional<WindowsProcessInfo>)> completion)
    {
        if (!completion || probe_completion_ || probe_process_->state() != QProcess::NotRunning)
        {
            return false;
        }

        const QString ps = QStandardPaths::findExecutable(QStringLiteral("ps"));
        if (ps.isEmpty())
        {
            SPDLOG_WARN("Alicia host process probe cannot run "
                        "because ps was not found");
            return false;
        }

        probe_output_.clear();
        probe_host_mode_ = true;
        probe_completion_ = std::move(completion);
        probe_process_->setProcessEnvironment(QProcessEnvironment::systemEnvironment());
        probe_process_->setWorkingDirectory(QDir::homePath());
        probe_process_->start(ps, {QStringLiteral("-axo"), QStringLiteral("pid=,command=")});
        probe_timeout_timer_->start(k_probe_timeout_ms);
        return true;
    }

    bool GameSession::start_game_lifecycle_probe(
        std::function<void(bool, std::optional<WindowsProcessInfo>)> completion)
    {
        if (!completion)
            return false;

        auto final_completion =
            std::make_shared<std::function<void(bool, std::optional<WindowsProcessInfo>)>>(
                std::move(completion));
#if defined(Q_OS_LINUX)
        return start_host_game_probe(
            [final_completion](const bool host_ok,
                               const std::optional<WindowsProcessInfo> host_info)
            { (*final_completion)(host_ok, host_info); });
#else
        auto fallback =
            [this, final_completion](const bool tasklist_ok,
                                     const std::optional<WindowsProcessInfo> tasklist_info)
        {
            if (tasklist_info)
            {
                (*final_completion)(true, tasklist_info);
                return;
            }

            if (!start_host_game_probe(
                    [final_completion, tasklist_ok](
                        const bool host_ok, const std::optional<WindowsProcessInfo> host_info)
                    { (*final_completion)(tasklist_ok || host_ok, host_info); }))
            {
                (*final_completion)(tasklist_ok, std::nullopt);
            }
        };
        if (start_game_probe(fallback))
            return true;
        return start_host_game_probe(
            [final_completion](const bool host_ok,
                               const std::optional<WindowsProcessInfo> host_info)
            { (*final_completion)(host_ok, host_info); });
#endif
    }

    void GameSession::handle_game_probe_output()
    {
        constexpr qsizetype k_max_probe_output = 512 * 1024;
        const QByteArray chunk = probe_process_->readAllStandardOutput();
        if (chunk.isEmpty())
            return;
        if (probe_output_.size() + chunk.size() > k_max_probe_output)
        {
            const qsizetype keep = qMax<qsizetype>(0, k_max_probe_output - chunk.size());
            probe_output_ = keep > 0 ? probe_output_.right(keep) : QByteArray();
        }
        probe_output_ += chunk.right(k_max_probe_output);
    }

    void GameSession::detect_existing_game()
    {
        if (phase_ != GamePhase::Idle || runner_.is_busy() || probe_completion_ ||
            !runtime_.is_wine_installed())
        {
            return;
        }
        const QString prefix = Config::instance().prefix_root();
        if (!QFileInfo(QDir(prefix).filePath(QStringLiteral("drive_c"))).isDir())
        {
            return;
        }

        if (!start_host_game_probe(
                [this](const bool ok, const std::optional<WindowsProcessInfo> info)
                {
                    if (!ok || !info || phase_ != GamePhase::Idle || runner_.is_busy())
                    {
                        return;
                    }
                    diagnostics_.reset();
                    tracked_version_ = Config::instance().game_version();
                    ++generation_;
                    attach_to_game_process(*info, SessionOrigin::Restored);
                }))
        {
            SPDLOG_DEBUG("existing Alicia process probe was already "
                         "active");
        }
    }

    void GameSession::begin_game_verification()
    {
        if (phase_ != GamePhase::Launching)
            return;
        diagnostics_.append_event(QStringLiteral("process_monitor_armed"),
                                  QStringLiteral("initial_delay_ms=%1 interval_ms=%2")
                                      .arg(k_initial_probe_delay_ms)
                                      .arg(k_start_probe_interval_ms));
        monitor_timer_->start(k_initial_probe_delay_ms);
    }

    void GameSession::poll_game_process()
    {
        if (phase_ != GamePhase::Launching && phase_ != GamePhase::Running)
        {
            return;
        }

        const quint64 generation = generation_;
        if (!start_game_lifecycle_probe(
                [this, generation](const bool ok, const std::optional<WindowsProcessInfo> info)
                {
                    if (generation != generation_ ||
                        (phase_ != GamePhase::Launching && phase_ != GamePhase::Running))
                    {
                        return;
                    }
                    on_probe_result(ok, info);
                }))
        {
            monitor_timer_->start(100);
        }
    }

    void GameSession::on_probe_result(const bool ok, const std::optional<WindowsProcessInfo> info)
    {
        if (phase_ != GamePhase::Launching && phase_ != GamePhase::Running)
        {
            return;
        }
        if (!ok)
        {
            on_probe_failure();
            return;
        }
        if (info)
        {
            on_process_seen(*info);
            return;
        }
        if (phase_ == GamePhase::Running)
        {
            on_process_missing();
            return;
        }
        on_still_launching();
    }

    void GameSession::on_probe_failure()
    {
        ++probe_failures_;
        SPDLOG_WARN("Alicia process verification failed ({}/{})", probe_failures_,
                    k_probe_failure_limit);
        if (probe_failures_ >= k_probe_failure_limit)
        {
            if (phase_ == GamePhase::Launching)
            {
                fail_game_launch(QStringLiteral("The compatibility process started, but the launcher "
                                                "could not verify whether Alicia.exe "
                                                "started."));
                return;
            }
            if (probe_failures_ >= k_attached_probe_failure_limit)
            {
#if defined(Q_OS_LINUX)
                stop_monitoring_uncertain(
                    QStringLiteral("The Linux host process check failed "
                                   "repeatedly. Monitoring has stopped "
                                   "without terminating Alicia. The game "
                                   "may still be running; close it normally, "
                                   "then restart the launcher before starting "
                                   "another session."));
#else
                stop_monitoring_uncertain(QStringLiteral("Both Windows and host process checks "
                                                         "failed repeatedly. Monitoring has "
                                                         "stopped without terminating Alicia. The "
                                                         "game may still be running; close it "
                                                         "normally, then restart the launcher "
                                                         "before starting another session."));
#endif
                return;
            }
        }

        monitor_timer_->start(phase_ == GamePhase::Running ? k_running_probe_interval_ms
                                                           : k_start_probe_interval_ms);
    }

    void GameSession::on_process_seen(const WindowsProcessInfo& info)
    {
        probe_failures_ = 0;
        missing_probes_ = 0;
        if (phase_ == GamePhase::Launching)
        {
            attach_to_game_process(info, SessionOrigin::Started);
            return;
        }
        if (phase_ != GamePhase::Running)
            return;

        const bool process_changed = tracked_game_process_.pid != info.pid;
        if (process_changed)
        {
            if (k_primary_probe_is_host)
            {
                SPDLOG_INFO("Alicia host process changed from PID {} "
                            "to PID {}",
                            tracked_game_process_.pid, info.pid);
            }
            else
            {
                SPDLOG_INFO("Alicia Windows process changed from PID "
                            "{} to PID {}",
                            tracked_game_process_.pid, info.pid);
            }
        }
        tracked_game_process_ = info;
        tracked_game_process_.source_line =
            redact_sensitive_text(tracked_game_process_.source_line);
        if (process_changed)
            tracked_host_process_ = {};
        if (k_primary_probe_is_host)
            tracked_host_process_ = tracked_game_process_;
        if (process_changed || tracked_host_process_.pid <= 0)
        {
            refresh_game_host_diagnostics();
        }
        monitor_timer_->start(k_running_probe_interval_ms);
    }

    void GameSession::on_process_missing()
    {
        if (phase_ != GamePhase::Running)
            return;
        ++missing_probes_;
        if (missing_probes_ >= k_missing_confirmation_limit)
        {
            finish_game_session(0, false);
            return;
        }
        monitor_timer_->start(k_running_probe_interval_ms);
    }

    void GameSession::on_still_launching()
    {
        if (phase_ != GamePhase::Launching)
            return;
        ++verification_attempts_;

        const auto diagnostic_configuration = diagnostics_.configuration();
        if (diagnostic_configuration.supported &&
            diagnostics_.elapsed_ms() >= k_absolute_start_timeout_ms)
        {
            diagnostics_.append_event(QStringLiteral("launcher_start_timeout"),
                                      QStringLiteral("timeout_ms=%1 attempts=%2 "
                                                     "wrapper_finished=%3 wrapper_exit=%4")
                                          .arg(k_absolute_start_timeout_ms)
                                          .arg(verification_attempts_)
                                          .arg(wrapper_state_ == WrapperState::Finished)
                                          .arg(wrapper_result_.exit_code));
            fail_game_launch(QStringLiteral("Alicia.exe was not observed within %1 "
                                            "seconds. The launcher ended monitoring "
                                            "instead of waiting forever.")
                                 .arg(k_absolute_start_timeout_ms / 1000));
            return;
        }

        if (wrapper_state_ == WrapperState::Finished && wrapper_finished_at_ms_ >= 0 &&
            QDateTime::currentMSecsSinceEpoch() - wrapper_finished_at_ms_ >=
                k_wrapper_exit_grace_ms)
        {
            diagnostics_.append_event(QStringLiteral("wrapper_exit_grace_expired"),
                                      QStringLiteral("grace_ms=%1 attempts=%2 wrapper_exit=%3 "
                                                     "crashed=%4")
                                          .arg(k_wrapper_exit_grace_ms)
                                          .arg(verification_attempts_)
                                          .arg(wrapper_result_.exit_code)
                                          .arg(wrapper_result_.crashed));
            fail_game_launch(QStringLiteral("The compatibility process exited and "
                                            "Alicia.exe was not observed within %1 "
                                            "seconds. The launcher stopped monitoring "
                                            "instead of leaving the Wine host in the "
                                            "background. Check launcher.log for the first "
                                            "missing library or graphics error.")
                                 .arg(k_wrapper_exit_grace_ms / 1000));
            return;
        }

        if (verification_attempts_ >= k_start_max_attempts)
        {
            QString detail = QStringLiteral("The compatibility process started, but Alicia.exe was "
                                            "never observed in the process list.");
            if (wrapper_state_ == WrapperState::Finished)
            {
                detail += QStringLiteral(" The compatibility process exited with "
                                         "code %1%2.")
                              .arg(wrapper_result_.exit_code)
                              .arg(wrapper_result_.crashed ? QStringLiteral(" after a crash")
                                                           : QString());
            }
            detail += QStringLiteral(" The launcher stayed in Launching instead "
                                     "of falsely reporting Running; check the "
                                     "launcher log for the first Wine or DLL "
                                     "error.");
            fail_game_launch(detail);
            return;
        }

        monitor_timer_->start(k_start_probe_interval_ms);
    }

    void GameSession::attach_to_game_process(const WindowsProcessInfo& info,
                                             const SessionOrigin origin)
    {
        if (info.pid <= 0 || phase_ == GamePhase::Running ||
            (phase_ != GamePhase::Idle && phase_ != GamePhase::Preflight &&
             phase_ != GamePhase::Launching))
        {
            return;
        }

        if (!transition(GamePhase::Running))
            return;
        origin_ = origin;
        tracked_game_process_ = info;
        tracked_game_process_.source_line =
            redact_sensitive_text(tracked_game_process_.source_line);
        tracked_host_process_ =
            k_primary_probe_is_host ? tracked_game_process_ : WindowsProcessInfo {};

        if (origin == SessionOrigin::Restored)
        {
            if (!probe_context_valid_)
                capture_game_probe_context();
            if (!launch_)
            {
                tracked_version_ = Config::instance().game_version();
            }
        }
        else if (launch_)
        {
            tracked_version_ = launch_->version;
        }

        if (launch_)
        {
            launch_->user.clear();
            launch_->token.clear();
        }

        if (k_primary_probe_is_host)
        {
            SPDLOG_INFO("attached lifecycle monitor to Alicia.exe "
                        "host PID {} (restored={}): {}",
                        tracked_game_process_.pid, origin == SessionOrigin::Restored,
                        tracked_game_process_.source_line.toStdString());
        }
        else
        {
            SPDLOG_INFO("attached lifecycle monitor to Alicia.exe "
                        "Windows PID {} (restored={}): {}",
                        tracked_game_process_.pid, origin == SessionOrigin::Restored,
                        tracked_game_process_.source_line.toStdString());
        }
        diagnostics_.append_event(QStringLiteral("alicia_process_observed"),
                                  QStringLiteral("windows_pid=%1 restored=%2")
                                      .arg(tracked_game_process_.pid)
                                      .arg(origin == SessionOrigin::Restored));
        if (origin != SessionOrigin::Restored && runner_.process_id() > 0)
        {
            SPDLOG_INFO("Wine launch wrapper host PID {}; Alicia.exe "
                        "is tracked independently",
                        runner_.process_id());
        }

        if (callbacks_.working)
        {
            callbacks_.working(QStringLiteral("game-running"), -1.0, true);
        }
        if (callbacks_.game_started)
            callbacks_.game_started(tracked_version_);
        monitor_timer_->start(k_running_probe_interval_ms);
        refresh_game_host_diagnostics();
    }

    void GameSession::refresh_game_host_diagnostics()
    {
        if (phase_ != GamePhase::Running || tracked_host_process_.pid > 0)
        {
            return;
        }

        const qint64 expected_windows_pid = tracked_game_process_.pid;
        if (!start_host_game_probe(
                [this, expected_windows_pid](const bool ok,
                                             const std::optional<WindowsProcessInfo> info)
                {
                    if (phase_ != GamePhase::Running ||
                        tracked_game_process_.pid != expected_windows_pid)
                    {
                        return;
                    }
                    if (!ok || !info)
                    {
                        SPDLOG_DEBUG("Alicia.exe Windows PID {} is confirmed, "
                                     "but its host PID was not resolved",
                                     tracked_game_process_.pid);
                        return;
                    }

                    tracked_host_process_ = *info;
                    tracked_host_process_.source_line =
                        redact_sensitive_text(tracked_host_process_.source_line);
                    SPDLOG_INFO("attached diagnostics to Alicia.exe host PID "
                                "{} for Windows PID {}: {}",
                                tracked_host_process_.pid, tracked_game_process_.pid,
                                tracked_host_process_.source_line.toStdString());
                    diagnostics_.append_event(QStringLiteral("alicia_host_process_observed"),
                                              QStringLiteral("host_pid=%1 windows_pid=%2")
                                                  .arg(tracked_host_process_.pid)
                                                  .arg(tracked_game_process_.pid));
                    const auto configuration = diagnostics_.configuration();
                    if (configuration.enabled && !diagnostic_sample_armed_)
                    {
                        diagnostic_sample_armed_ = true;
                        diagnostics_.arm_host_sample(tracked_game_process_.pid,
                                                     tracked_host_process_.pid);
                    }
                }))
        {
            SPDLOG_DEBUG("host PID diagnostics probe deferred because "
                         "another probe is active");
        }
    }

    void GameSession::handle_wrapper_finished(const command_result& result,
                                              const quint64 generation)
    {
        if (generation != generation_ ||
            (phase_ != GamePhase::Launching && phase_ != GamePhase::Running))
        {
            return;
        }

        wrapper_result_ = result;
        wrapper_state_ = WrapperState::Finished;
        wrapper_finished_at_ms_ = QDateTime::currentMSecsSinceEpoch();
        if (!result.started && result.outcome == CommandOutcome::FailedToStart)
        {
            fail_game_launch(QStringLiteral("Wine could not start the game launch "
                                            "process: %1")
                                 .arg(result.error_message));
            return;
        }

        SPDLOG_INFO("Wine launch wrapper finished (exit {}, "
                    "crashed={}); Alicia.exe process monitoring "
                    "remains authoritative",
                    result.exit_code, result.crashed);
        diagnostics_.append_event(QStringLiteral("wine_wrapper_finished"),
                                  QStringLiteral("exit_code=%1 crashed=%2 game_confirmed=%3")
                                      .arg(result.exit_code)
                                      .arg(result.crashed)
                                      .arg(phase_ == GamePhase::Running));
        monitor_timer_->start(0);
    }

    command_result GameSession::current_session_result() const
    {
        if (wrapper_state_ == WrapperState::Finished)
            return wrapper_result_;
        if (runner_.is_busy())
            return runner_.snapshot();
        if (last_stage_result_.started || !last_stage_result_.output.isEmpty() ||
            !last_stage_result_.error_message.isEmpty())
        {
            return last_stage_result_;
        }
        command_result result;
        result.started = origin_ == SessionOrigin::Restored || phase_ == GamePhase::Running;
        return result;
    }

    QString GameSession::diagnostic_suffix() const
    {
        if (diagnostics_.diagnostic_path().isEmpty())
            return {};
        return util::i18n::translate("\n\nDiagnostic run: %1")
            .arg(QFileInfo(diagnostics_.diagnostic_path()).absolutePath());
    }

    void GameSession::fail_game_launch(const QString& message)
    {
        if (phase_ == GamePhase::Idle || phase_ == GamePhase::Finished ||
            phase_ == GamePhase::MonitoringUncertain)
        {
            return;
        }

        const bool had_confirmed_game = phase_ == GamePhase::Running;
        const auto version = tracked_version_;
        const int wrapper_exit = wrapper_result_.exit_code;
        const bool wrapper_crashed = wrapper_result_.crashed;
        command_result result = current_session_result();
        QString user_message = util::i18n::translate(message);
        user_message += diagnostic_suffix();

        if (!transition(GamePhase::Finished))
            return;
        stop_probe();
        diagnostics_.cancel_host_sample();
        if (runner_.is_busy())
            runner_.terminate(CommandOutcome::Cancelled);

        diagnostics_.finish(had_confirmed_game
                                ? QStringLiteral("monitor_failure_after_process_seen")
                                : QStringLiteral("launch_failed"),
                            wrapper_exit, wrapper_crashed, message);
        diagnostics_.close_log();

        result.outcome = CommandOutcome::FailedToStart;
        result.error_message = user_message;
        result.crashed = wrapper_crashed;
        result.exit_code = wrapper_exit;
        if (callbacks_.command_finished)
            callbacks_.command_finished(result);
        if (had_confirmed_game && callbacks_.game_exited)
        {
            callbacks_.game_exited(version, wrapper_exit, wrapper_crashed);
        }
        if (callbacks_.fail_user)
        {
            callbacks_.fail_user(QStringLiteral("Game Launch Failed"), user_message);
        }

        clear_session_state();
        (void)transition(GamePhase::Idle);
    }

    void GameSession::stop_monitoring_uncertain(const QString& message)
    {
        if (phase_ != GamePhase::Running)
            return;

        QString user_message = util::i18n::translate(message);
        user_message += diagnostic_suffix();
        const int last_wrapper_exit = wrapper_result_.exit_code;
        command_result result = current_session_result();

        if (!transition(GamePhase::MonitoringUncertain))
        {
            return;
        }
        stop_probe();
        diagnostics_.cancel_host_sample();
        diagnostics_.finish(QStringLiteral("monitoring_lost"), last_wrapper_exit, false, message);
        diagnostics_.close_log();

        result.outcome = CommandOutcome::FailedToStart;
        result.exit_code = -1;
        result.crashed = false;
        result.error_message = user_message;
        if (callbacks_.command_finished)
            callbacks_.command_finished(result);
        if (callbacks_.fail_user)
        {
            callbacks_.fail_user(QStringLiteral("Game Monitoring Stopped"), user_message);
        }
        if (callbacks_.working)
        {
            callbacks_.working(QStringLiteral("game-monitoring-uncertain"), -1.0, true);
        }

        clear_session_state();
    }

    void GameSession::finish_game_session(const int exit_code, const bool crashed)
    {
        if (phase_ != GamePhase::Running)
            return;

        const bool trace_fatal_failure = diagnostics_.trace_has_fatal_failure();
        const bool effective_crashed = crashed || trace_fatal_failure;
        const auto version = tracked_version_;
        const bool restored = origin_ == SessionOrigin::Restored;
        const qint64 windows_pid = tracked_game_process_.pid;
        const qint64 host_pid = tracked_host_process_.pid;
        const int wrapper_exit = wrapper_result_.exit_code;
        const bool wrapper_crashed = wrapper_result_.crashed;
        const QString executable_path = launch_ ? launch_->executable_path : QString();
        command_result result = current_session_result();

        if (!transition(GamePhase::Finished))
            return;
        stop_probe();
        diagnostics_.cancel_host_sample();

        const qint64 duration_ms = diagnostics_.elapsed_ms();
        QString timeline_outcome;
        if (effective_crashed)
            timeline_outcome = QStringLiteral("crash");
        else if (duration_ms < 15000)
            timeline_outcome = QStringLiteral("early_exit");
        else if (duration_ms >= 60000)
            timeline_outcome = QStringLiteral("long_session_exit");
        else
            timeline_outcome = QStringLiteral("short_session_exit");
        diagnostics_.finish(timeline_outcome, exit_code, effective_crashed,
                            QStringLiteral("windows_pid=%1 host_pid=%2 "
                                           "wrapper_exit=%3 fatal_trace=%4 "
                                           "present_seen=%5 draw_seen=%6")
                                .arg(windows_pid)
                                .arg(host_pid)
                                .arg(wrapper_exit)
                                .arg(trace_fatal_failure)
                                .arg(diagnostics_.saw_present())
                                .arg(diagnostics_.saw_draw()));
        diagnostics_.append_footer_and_analyze(
            QStringLiteral("--- Alicia process disappeared; "
                           "wrapper exit=%1 wrapper_crashed=%2 "
                           "fatal_trace=%3 effective_crashed=%4 ---\n")
                .arg(wrapper_exit)
                .arg(wrapper_crashed)
                .arg(trace_fatal_failure)
                .arg(effective_crashed),
            executable_path);

        result.started = true;
        result.exit_code = exit_code;
        result.crashed = effective_crashed;
        result.outcome = effective_crashed ? CommandOutcome::Crashed
                                           : (exit_code == 0 ? CommandOutcome::Success
                                                             : CommandOutcome::NonZeroExit);

        if (k_primary_probe_is_host)
        {
            SPDLOG_INFO("Alicia.exe host PID {} is no longer present "
                        "(restored={}, wrapper_exit={})",
                        windows_pid, restored, wrapper_exit);
        }
        else
        {
            SPDLOG_INFO("Alicia.exe Windows PID {} is no longer "
                        "present (host_pid={}, restored={}, "
                        "wrapper_exit={})",
                        windows_pid, host_pid, restored, wrapper_exit);
        }

        if (callbacks_.command_finished)
            callbacks_.command_finished(result);
        if (callbacks_.game_exited)
        {
            callbacks_.game_exited(version, exit_code, effective_crashed);
        }
        if (effective_crashed || exit_code != 0)
        {
            if (callbacks_.failed)
            {
                const QString status_message = diagnostics_.diagnostic_path().isEmpty()
                                                   ? QStringLiteral("The game exited unexpectedly.")
                                                   : QStringLiteral("The game exited unexpectedly. "
                                                                    "Diagnostic log: %1")
                                                         .arg(diagnostics_.diagnostic_path());
                callbacks_.failed(status_message);
            }
        }
        else if (callbacks_.done)
        {
            callbacks_.done(QStringLiteral("Game exited."));
        }

        if (runner_.is_busy())
            runner_.terminate(CommandOutcome::Cancelled);
        clear_session_state();
        (void)transition(GamePhase::Idle);
    }

    void GameSession::cancel()
    {
        if (phase_ == GamePhase::Idle)
        {
            if (probe_completion_ || probe_process_->state() != QProcess::NotRunning)
            {
                stop_probe();
            }
            return;
        }
        if (phase_ == GamePhase::Running)
        {
            if (callbacks_.user_notice)
            {
                callbacks_.user_notice(QStringLiteral("Alicia.exe is running. Exit the game "
                                                      "normally instead of cancelling its "
                                                      "monitor."));
            }
            return;
        }
        if (phase_ == GamePhase::MonitoringUncertain)
        {
            if (callbacks_.user_notice)
            {
                callbacks_.user_notice(QStringLiteral("The game may still be running. Close it "
                                                      "normally and restart the launcher before "
                                                      "starting another session."));
            }
            return;
        }
        fail_game_launch(QStringLiteral("The game launch was cancelled."));
    }

    void GameSession::launch_game_process()
    {
        if (phase_ != GamePhase::Launching || !launch_)
        {
            return;
        }

        const auto& profile = core::game::profile(launch_->version);
        const QString prefix = Config::instance().prefix_root();
#if defined(Q_OS_MACOS)
        constexpr bool proton = false;
#else
        const bool proton = runtime_.runtime_is_proton();
#endif
        const bool developer_mode = util::launch_arguments::developer_mode_enabled();
        const auto custom_arguments =
            util::launch_arguments::validate(Config::instance().game_args());
        const QString sensitive_token =
            developer_mode && custom_arguments.valid && !custom_arguments.developer_op.isEmpty()
                ? custom_arguments.developer_op
                : launch_->token;
        const auto diagnostics_configuration = diagnostics_.begin(
            launch_->version, prefix, launch_->game_directory, launch_->executable_path,
            {sensitive_token},
            [this]()
            {
                return MacLaunchDiagnostics::ProcessSnapshot {
                    phase_ == GamePhase::Launching || phase_ == GamePhase::Running,
                    phase_ == GamePhase::Running,
                    wrapper_state_ == WrapperState::Running ||
                        runner_.state() != QProcess::NotRunning};
            });

#if defined(Q_OS_MACOS)
        const bool requested_dxvk = false;
        const bool effective_dxvk = false;
        const QString compatibility_profile = diagnostics_configuration.profile;
        const QString runtime_executable = diagnostics_configuration.runtime.executable;
        const bool force_windowed_d3d9 =
            compatibility_profile == QStringLiteral("safe-display") ||
            compatibility_profile == QStringLiteral("low-graphics");
#else
        const bool requested_dxvk = Config::instance().use_dxvk();
        const bool effective_dxvk =
            requested_dxvk && (proton || PrefixInspector::dxvk_installed(prefix));
        constexpr bool force_windowed_d3d9 = false;
#endif
#if defined(Q_OS_MACOS)
        const QString injector_marker_dir =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        const bool bypass_alicia_injector =
            (qEnvironmentVariableIntValue("SOA_BYPASS_ALICIA_INJECTOR") == 1 ||
             QFileInfo(QDir(injector_marker_dir)
                           .filePath(QStringLiteral("no-injector")))
                 .exists()) &&
            !force_windowed_d3d9;
        if (bypass_alicia_injector)
        {
            SPDLOG_INFO("Alicia injector bypassed (no hook, no audio this run)");
        }
#else
        constexpr bool bypass_alicia_injector = false;
#endif
        if (requested_dxvk && !effective_dxvk)
        {
            if (callbacks_.user_notice)
            {
                callbacks_.user_notice(QStringLiteral("DXVK was requested but is not installed. "
                                                      "Falling back to WineD3D for this launch."));
            }
        }

        QString path_error;
        const QString windows_executable =
            windows_path_for_prefix_file(prefix, launch_->executable_path, &path_error);
        if (windows_executable.isEmpty())
        {
            fail_game_launch(path_error);
            return;
        }

        AliciaLogHook::Preparation alicia_log_hook;
        if (!bypass_alicia_injector)
        {
#if defined(Q_OS_MACOS)
            const bool audio_needs_hook =
                qEnvironmentVariable("SOA_AUDIO_PIPE").trimmed() != QStringLiteral("0") &&
                qEnvironmentVariable("SOA_AUDIO_NULL_BACKEND").trimmed() != QStringLiteral("1");
#else
            constexpr bool audio_needs_hook = false;
#endif
            alicia_log_hook = AliciaLogHook::prepare(
                prefix, diagnostics_.run_directory(), force_windowed_d3d9,
                audio_needs_hook);
        }
        else
        {
            diagnostics_.append_event(
                QStringLiteral("alicia_log_hook_bypassed"),
                QStringLiteral("launcher arguments and authentication preserved; "
                               "injector and hook disabled by SOA_BYPASS_ALICIA_INJECTOR=1"));
            SPDLOG_INFO("Alicia injector bypass enabled; launching Alicia.exe directly with "
                        "launcher-managed authentication arguments");
        }
        if (alicia_log_hook.requested)
        {
            if (alicia_log_hook.available)
            {
                diagnostics_.append_event(
                    QStringLiteral("alicia_log_hook_ready"),
                    QStringLiteral("output=%1 upstream=SergeantSerk/log-hook "
                                   "force_windowed=%2")
                        .arg(alicia_log_hook.log_host_path)
                        .arg(force_windowed_d3d9));
            }
            else
            {
                diagnostics_.append_event(QStringLiteral("alicia_log_hook_unavailable"),
                                          alicia_log_hook.failure);
                SPDLOG_WARN("Alicia hook unavailable: {}",
                            alicia_log_hook.failure.toStdString());
                if (force_windowed_d3d9)
                {
                    fail_game_launch(QStringLiteral(
                        "The 1024x720 windowed D3D9 compatibility hook could not be prepared: %1")
                                         .arg(alicia_log_hook.failure));
                    return;
                }
                if (callbacks_.user_notice)
                    callbacks_.user_notice(alicia_log_hook.failure);
            }
        }

        QStringList game_arguments;
        game_arguments << QStringLiteral("-GameID")
                       << (developer_mode
                               ? QStringLiteral("2")
                               : QString::fromLatin1(profile.launch_game_id));
        if (!custom_arguments.valid)
        {
            fail_game_launch(
                QStringLiteral("Invalid launch arguments: %1").arg(custom_arguments.error));
            return;
        }
        if (developer_mode)
        {
            if (custom_arguments.developer_id.isEmpty() || custom_arguments.developer_op.isEmpty())
            {
                fail_game_launch(QStringLiteral(
                    "Developer mode requires -ID and -OP in Game Launch Arguments."));
                return;
            }
            game_arguments << QStringLiteral("-ID") << custom_arguments.developer_id
                           << QStringLiteral("-OP") << custom_arguments.developer_op;
            SPDLOG_INFO("Developer mode enabled: using GameID 2 with custom local credentials; "
                        "aliciadev must resolve to the local server");
        }
        else
        {
            game_arguments << QStringLiteral("-ID") << QStringLiteral("[%1]").arg(launch_->user)
                           << QStringLiteral("-OP") << QStringLiteral("[%1]").arg(launch_->token);
        }
        game_arguments.append(custom_arguments.arguments);

#if defined(Q_OS_MACOS)
        const QStringList expected_local_files {
            QStringLiteral("d3dx9_31.dll"),       QStringLiteral("d3dx9_42.dll"),
            QStringLiteral("D3DCompiler_42.dll"), QStringLiteral("xinput1_3.dll"),
            QStringLiteral("msvcp100.dll"),       QStringLiteral("msvcr100.dll"),
            QStringLiteral("PhysXLoader.dll"),    QStringLiteral("PhysXCore.dll"),
            QStringLiteral("PhysXCooking.dll"),   QStringLiteral("PhysXDevice.dll"),
            QStringLiteral("cudart32_30_9.dll")};
        QStringList missing_local_files;
        for (const QString& file : expected_local_files)
        {
            if (!QFileInfo(QDir(launch_->game_directory).filePath(file)).isFile())
            {
                missing_local_files.append(file);
            }
        }
        if (!missing_local_files.isEmpty())
        {
            fail_game_launch(QStringLiteral("The macOS game package is incomplete. "
                                            "Repair the game installation; these required "
                                            "local components are missing:\n%1")
                                 .arg(missing_local_files.join(QStringLiteral("\n"))));
            return;
        }

        if (compatibility_profile == QStringLiteral("low-graphics"))
        {
            QString patch_message;
            if (!patch_existing_alice_config(launch_->game_directory, patch_message))
            {
                fail_game_launch(
                    QStringLiteral("Compatibility profile failed: %1").arg(patch_message));
                return;
            }
            if (callbacks_.user_notice)
                callbacks_.user_notice(patch_message);
        }
#endif

        const quint64 generation = generation_;
        auto start_wrapper = [this, generation,
                              sensitive_token](const QString& program, const QStringList& arguments,
                                               const QProcessEnvironment& environment,
                                               const QString& failure_message)
        {
            ProcessRunner::Request request;
            request.program = program;
            request.arguments = arguments;
            request.environment = environment;
            request.working_directory = launch_ ? launch_->game_directory : QString();
            request.sensitive_values = {sensitive_token};
            request.prevent_system_sleep = true;
            request.started = [this, generation](const qint64 host_pid)
            {
                if (generation != generation_ || phase_ != GamePhase::Launching)
                {
                    return;
                }
                wrapper_state_ = WrapperState::Running;
                diagnostics_.append_event(QStringLiteral("wine_wrapper_started"),
                                          QStringLiteral("host_pid=%1").arg(host_pid));
                if (callbacks_.working)
                {
                    callbacks_.working(QStringLiteral("game-launching"), -1.0, true);
                }
                begin_game_verification();
            };
            request.output = [this, generation](const QString& chunk)
            {
                if (generation == generation_)
                    diagnostics_.observe_output(chunk);
            };

            diagnostics_.write_effective_command(
                program, redacted_command_args(arguments, {sensitive_token}),
                request.working_directory);
#if defined(Q_OS_MACOS)
            if (diagnostics_.configuration().supported &&
                QFileInfo(QStringLiteral("/usr/bin/caffeinate")).isExecutable())
            {
                diagnostics_.append_event(QStringLiteral("app_nap_guard_enabled"),
                                          QStringLiteral("program=/usr/bin/caffeinate "
                                                         "flags=-dimsu"));
            }
#endif

            if (runner_.start(std::move(request), [this, generation](const command_result& result)
                              { handle_wrapper_finished(result, generation); }))
            {
                return true;
            }
            fail_game_launch(failure_message);
            return false;
        };

#if !defined(Q_OS_MACOS)
        if (proton)
        {
            const QString umu = umu_path();
            if (umu.isEmpty() || !QFileInfo(umu).isExecutable())
            {
                fail_game_launch(QStringLiteral("Proton launch requires a working umu-run "
                                                "installation."));
                return;
            }

            QProcessEnvironment environment = runtime_.umu_environment();
            environment.insert(QStringLiteral("STEAM_COMPAT_LIBRARY_PATHS"),
                               launch_->game_directory + QLatin1Char(':') + prefix);
            if (effective_dxvk)
                environment.remove(QStringLiteral("PROTON_USE_WINED3D"));
            else
                environment.insert(QStringLiteral("PROTON_USE_WINED3D"), QStringLiteral("1"));
            RuntimeLocator::apply_runtime_environment_entries(
                environment, custom_arguments.environment_entries);

            if (alicia_log_hook.available)
            {
                if (!alicia_log_hook.log_windows_path.isEmpty())
                {
                    environment.insert(QStringLiteral("SOA_ALICIA_LOG_PATH"),
                                       alicia_log_hook.log_windows_path);
                    environment.insert(QStringLiteral("SOA_ALICIA_LOG_REDACT"), sensitive_token);
                    environment.insert(QStringLiteral("SOA_ALICIA_LOG_ALL"), QStringLiteral("0"));
                }
                if (force_windowed_d3d9)
                {
                    environment.insert(QStringLiteral("SOA_D3D9_FORCE_WINDOWED"),
                                       QStringLiteral("1"));
                    environment.insert(QStringLiteral("SOA_D3D9_WINDOW_WIDTH"),
                                       QStringLiteral("1024"));
                    environment.insert(QStringLiteral("SOA_D3D9_WINDOW_HEIGHT"),
                                       QStringLiteral("720"));
                }
            }

            QStringList arguments;
            if (alicia_log_hook.available)
            {
                arguments << alicia_log_hook.injector_host_path << windows_executable;
            }
            else
            {
                arguments << launch_->executable_path;
            }
            arguments.append(game_arguments);
            start_wrapper(umu, arguments, environment,
                          QStringLiteral("umu-run could not be started."));
            return;
        }
#endif

        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("WINEPREFIX"), prefix);
#if defined(Q_OS_MACOS)
        environment.insert(QStringLiteral("WINEARCH"), QStringLiteral("win64"));
        environment.insert(QStringLiteral("WINEDEBUG"), diagnostics_configuration.wine_debug);
        const QString graphics_marker_dir =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        const bool wined3d_forced =
            qEnvironmentVariable("SOA_D3D_BACKEND").trimmed() ==
                QStringLiteral("wined3d") ||
            QFileInfo(QDir(graphics_marker_dir)
                          .filePath(QStringLiteral("use-wined3d")))
                .exists();

        QString macos_dll_overrides =
            QStringLiteral("d3d9,ddraw,dinput8,dsound=b;"
                           "d3dx9_31,d3dx9_42,d3dcompiler_42,"
                           "d3dcompiler_47,msvcp100,msvcr100=n,b");
        SPDLOG_INFO("Graphics backend: builtin wined3d (OpenGL){}",
                    wined3d_forced ? " (forced)" : "");
        const bool audio_pipe =
            qEnvironmentVariable("SOA_AUDIO_PIPE").trimmed() != QStringLiteral("0") &&
            qEnvironmentVariable("SOA_AUDIO_NULL_BACKEND").trimmed() != QStringLiteral("1");
        if (audio_pipe && alicia_log_hook.available)
        {
            const QString app_data =
                QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            QDir().mkpath(app_data);
            const QString helper =
                QDir(app_data).filePath(QStringLiteral("soa-audio-host"));

            QStringList helper_sources;
            const QString helper_override =
                qEnvironmentVariable("SOA_AUDIO_HOST_PATH").trimmed();
            if (!helper_override.isEmpty())
            {
                helper_sources << helper_override;
            }
            if (!alicia_log_hook.audio_host_host_path.isEmpty())
            {
                helper_sources << alicia_log_hook.audio_host_host_path;
            }
            const QDir application_dir(QCoreApplication::applicationDirPath());
            helper_sources
                << application_dir.filePath(
                       QStringLiteral("alicia-log-hook/soa-audio-host"))
                << application_dir.filePath(
                       QStringLiteral("../Resources/alicia-log-hook/soa-audio-host"))
                << application_dir.filePath(QStringLiteral("soa-audio-host"));

            const QFileInfo installed(helper);
            for (const QString& raw_source : helper_sources)
            {
                const QFileInfo source(QDir::cleanPath(raw_source));
                if (!source.isFile())
                {
                    continue;
                }
                if (installed.exists() &&
                    installed.lastModified() >= source.lastModified())
                {
                    break;
                }
                QFile::remove(helper);
                if (QFile::copy(source.absoluteFilePath(), helper))
                {
                    SPDLOG_INFO("Installed audio helper from {} to {}",
                                source.absoluteFilePath().toStdString(),
                                helper.toStdString());
                }
                break;
            }

            if (QFileInfo(helper).isFile())
            {
                QFile::setPermissions(helper, QFile::permissions(helper) |
                                                  QFileDevice::ExeOwner |
                                                  QFileDevice::ExeGroup |
                                                  QFileDevice::ExeOther);
            }

            if (QFileInfo(helper).isExecutable())
            {
                const QString port =
                    qEnvironmentVariable("SOA_AUDIO_PIPE_PORT").trimmed();
                QStringList helper_arguments { QStringLiteral("--exit-after-stream") };
                if (!port.isEmpty())
                {
                    helper_arguments << QStringLiteral("--port") << port;
                }
                if (QProcess::startDetached(helper, helper_arguments))
                {
                    environment.insert(QStringLiteral("SOA_AUDIO_PIPE"),
                                       QStringLiteral("1"));
                    if (!port.isEmpty())
                    {
                        environment.insert(QStringLiteral("SOA_AUDIO_PIPE_PORT"), port);
                    }
                    SPDLOG_INFO("Started native audio helper: {}",
                                helper.toStdString());
                }
                else
                {
                    SPDLOG_WARN("Could not start {}; falling back to silent audio",
                                helper.toStdString());
                }
            }
            else
            {
                SPDLOG_WARN("No soa-audio-host at {} and no source to install "
                            "from; audio will be silent. Copy it there by hand "
                            "or set SOA_AUDIO_HOST_PATH. Sources tried:",
                            helper.toStdString());
                for (const QString& candidate : helper_sources)
                {
                    SPDLOG_WARN("  {}", QDir::cleanPath(candidate).toStdString());
                }
            }
        }

        if (!environment.contains(QStringLiteral("SOA_AUDIO_PIPE")) &&
            MacLaunchDiagnostics::profile_disables_audio(compatibility_profile))
        {

            environment.insert(QStringLiteral("SOA_AUDIO_NULL_BACKEND"), QStringLiteral("1"));
            SPDLOG_INFO("Audio isolation: enabling in-process null DirectSound backend");
        }

        const QString custom_wine_overrides =
            qEnvironmentVariable("SOA_WINE_DLL_OVERRIDES").trimmed();
        if (!custom_wine_overrides.isEmpty())
        {
            macos_dll_overrides += QLatin1Char(';') + custom_wine_overrides;
            SPDLOG_INFO("Appending custom Wine DLL overrides: {}",
                        custom_wine_overrides.toStdString());
        }
        environment.insert(QStringLiteral("WINEDLLOVERRIDES"), macos_dll_overrides);
        SPDLOG_INFO("Effective Alicia WINEDLLOVERRIDES: {}",
                    macos_dll_overrides.toStdString());
        environment.insert(QStringLiteral("WINE_FULLSCREEN_FSR"), QStringLiteral("0"));

        if (QFileInfo(QDir(graphics_marker_dir).filePath(QStringLiteral("use-esync")))
                .exists() ||
            qEnvironmentVariable("SOA_ENABLE_ESYNC").trimmed() == QStringLiteral("1"))
        {
            environment.insert(QStringLiteral("WINEESYNC"), QStringLiteral("1"));
            SPDLOG_INFO("esync enabled (opt-in)");
        }
        if (QFileInfo(QDir(graphics_marker_dir).filePath(QStringLiteral("use-msync")))
                .exists() ||
            qEnvironmentVariable("SOA_ENABLE_MSYNC").trimmed() == QStringLiteral("1"))
        {
            environment.insert(QStringLiteral("WINEMSYNC"), QStringLiteral("1"));
            SPDLOG_INFO("msync enabled (opt-in)");
        }

        if (QFileInfo(QDir(graphics_marker_dir).filePath(QStringLiteral("metal-hud")))
                .exists())
        {
            environment.insert(QStringLiteral("MTL_HUD_ENABLED"), QStringLiteral("1"));
            SPDLOG_INFO("Metal performance HUD enabled");
        }
#else
        environment.insert(QStringLiteral("WINEDEBUG"), diagnostics_configuration.wine_debug);
        environment.insert(QStringLiteral("WINEDLLOVERRIDES"),
                           effective_dxvk ? QStringLiteral("d3d9,d3d10core,d3d11,dxgi=n")
                                          : QStringLiteral("d3d9,d3d10core,d3d11,dxgi=b"));
#endif
        runtime_.apply_wine_environment(environment);
        RuntimeLocator::apply_runtime_environment_entries(
            environment, custom_arguments.environment_entries);
        if (alicia_log_hook.available)
        {
            if (!alicia_log_hook.log_windows_path.isEmpty())
            {
                environment.insert(QStringLiteral("SOA_ALICIA_LOG_PATH"),
                                   alicia_log_hook.log_windows_path);
                environment.insert(QStringLiteral("SOA_ALICIA_LOG_REDACT"), sensitive_token);
                environment.insert(QStringLiteral("SOA_ALICIA_LOG_ALL"), QStringLiteral("0"));
            }
            if (force_windowed_d3d9)
            {
                environment.insert(QStringLiteral("SOA_D3D9_FORCE_WINDOWED"),
                                   QStringLiteral("1"));
                environment.insert(QStringLiteral("SOA_D3D9_WINDOW_WIDTH"),
                                   QStringLiteral("1024"));
                environment.insert(QStringLiteral("SOA_D3D9_WINDOW_HEIGHT"),
                                   QStringLiteral("720"));
            }
        }

        QStringList arguments;
#if defined(Q_OS_MACOS)
        const bool virtual_desktop =
            MacLaunchDiagnostics::profile_uses_virtual_desktop(compatibility_profile);
        if (virtual_desktop)
        {

            static const QRegularExpression geometry_pattern(
                QStringLiteral("^[0-9]{3,5}x[0-9]{3,5}$"));
            QString geometry = qEnvironmentVariable("SOA_VIRTUAL_DESKTOP").trimmed();
            if (!geometry_pattern.match(geometry).hasMatch())
                geometry = QString::fromLatin1(k_default_virtual_desktop);
            arguments << QStringLiteral("explorer")
                      << QStringLiteral("/desktop=StoryOfAlicia,%1").arg(geometry);
            SPDLOG_INFO("Virtual desktop geometry {}", geometry.toStdString());
        }
        if (alicia_log_hook.available)
        {
            arguments << alicia_log_hook.injector_windows_path;
        }
        arguments << windows_executable;
        arguments.append(game_arguments);

        const auto& runtime_context = diagnostics_configuration.runtime;
        if (runtime_context.usable)
        {
            SPDLOG_INFO("Alicia Wine session: source={} executable={} profile={}",
                        runtime_context.source.toStdString(),
                        runtime_context.executable.toStdString(),
                        compatibility_profile.toStdString());
        }
        const QStringList logged_arguments = redacted_command_args(arguments, {sensitive_token});
        SPDLOG_INFO("macOS launch profile={} runtime={} prefix={} "
                    "executable={} virtual_desktop={} retina={} "
                    "opengl_surface={} command={} {}",
                    compatibility_profile.toStdString(), runtime_executable.toStdString(),
                    prefix.toStdString(), windows_executable.toStdString(), virtual_desktop,
                    MacLaunchDiagnostics::retina_enabled(compatibility_profile) ? "on" : "off",
                    MacLaunchDiagnostics::opengl_surface_mode(compatibility_profile).isEmpty()
                        ? "front-opaque-default"
                        : "behind",
                    runtime_.wine_binary().toStdString(),
                    logged_arguments.join(QLatin1Char(' ')).toStdString());
        const QString launch_failure = diagnostics_.diagnostic_path().isEmpty()
            ? QStringLiteral("Wine could not start Alicia.exe. Check launcher.log for details.")
            : QStringLiteral("Wine could not start Alicia.exe. Check the labeled diagnostic run at %1.")
                  .arg(QFileInfo(diagnostics_.diagnostic_path()).absolutePath());
        start_wrapper(runtime_.wine_binary(), arguments, environment, launch_failure);
#else
        if (alicia_log_hook.available)
            arguments << alicia_log_hook.injector_windows_path;
        arguments << windows_executable;
        arguments.append(game_arguments);
        start_wrapper(runtime_.wine_binary(), arguments, environment,
                      QStringLiteral("Wine could not start Alicia.exe."));
#endif
    }
}
