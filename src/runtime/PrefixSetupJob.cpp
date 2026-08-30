#include "runtime/PrefixSetupJob.hpp"

#include <QDir>
#include <QFileInfo>
#include <QTimer>

#include <utility>

#include "runtime/MacWineRuntime.hpp"
#include "runtime/PrefixInspector.hpp"
#include "runtime/RuntimeLocator.hpp"
#include "runtime/WineRegistry.hpp"
#include "config/Config.hpp"
#include <spdlog/spdlog.h>

namespace core::wine
{
    using util::config::Config;

    namespace
    {
        constexpr int k_runtime_probe_timeout_ms = 15 * 1000;
        constexpr int k_registry_timeout_ms = 45 * 1000;
        constexpr int k_prefix_validation_retries = 20;
        constexpr int k_prefix_validation_retry_ms = 500;
        const QString k_installing_message = QStringLiteral("Installing components "
                                                            "(this can take a while)...");
    }

    PrefixSetupJob::PrefixSetupJob(RuntimeLocator& runtime, ProcessRunner& runner,
                                   Callbacks callbacks, QObject* parent)
        : QObject(parent), runtime_(runtime), runner_(runner), callbacks_(std::move(callbacks))
    {
    }

    bool PrefixSetupJob::is_active() const
    {
        return active_;
    }

    bool PrefixSetupJob::ensure_idle()
    {
        if (!active_ && !runner_.is_busy())
            return true;

        const QString busy_message =
            QStringLiteral("Another Wine or game process is already running.");
        if (callbacks_.fail_user)
        {
            callbacks_.fail_user(QStringLiteral("Launcher Busy"), busy_message);
        }
        if (callbacks_.setup_finished)
            callbacks_.setup_finished(false);
        return false;
    }

    void PrefixSetupJob::setup()
    {
        if (!ensure_idle())
            return;
        if (runtime_.runtime_is_proton())
            setup_proton();
        else
            setup_wine();
    }

    QStringList PrefixSetupJob::missing_component_packages() const
    {
        return PrefixInspector::missing_packages(Config::instance().prefix_root(),
                                                 runtime_.runtime_is_proton(), false);
    }

    bool PrefixSetupJob::queue_missing_components()
    {
        const QStringList packages = missing_component_packages();
#if defined(Q_OS_MACOS)
        const bool install_optional_dxvk = false;
#else
        const bool install_optional_dxvk =
            Config::instance().use_dxvk() && !runtime_.runtime_is_proton() &&
            !PrefixInspector::dxvk_installed(Config::instance().prefix_root());
#endif
        if (packages.isEmpty() && !install_optional_dxvk)
            return true;

        const bool proton = runtime_.runtime_is_proton();
        const bool umu_winetricks =
            proton && WineRegistry::proton_supports_umu_winetricks(runtime_.proton_root());
        QString installer;
        QStringList package_arguments;
        QProcessEnvironment package_environment;

        if (umu_winetricks)
        {
            installer = umu_path();
            if (installer.isEmpty() || !QFileInfo(installer).isExecutable())
            {
                SPDLOG_ERROR("Proton component setup requires umu-run");
                return false;
            }
            package_arguments << QStringLiteral("winetricks");
            package_arguments.append(packages);
            package_environment = runtime_.umu_environment();
            package_environment.insert(QStringLiteral("PROTON_VERB"),
                                       QStringLiteral("runinprefix"));
            SPDLOG_INFO("installing Proton components through umu-run winetricks: {}",
                        packages.join(QStringLiteral(", ")).toStdString());
        }
        else
        {
            installer = winetricks_path();
            if (installer.isEmpty() || !QFileInfo(installer).isExecutable())
            {
                if (!packages.isEmpty())
                {
                    if (proton)
                    {
                        SPDLOG_ERROR("selected Proton cannot use UMU Winetricks; install "
                                     "GE-Proton or UMU-Proton, or install host winetricks: {}",
                                     packages.join(QStringLiteral(", ")).toStdString());
                    }
                    else
                    {
                        SPDLOG_ERROR("missing components require winetricks: {}",
                                     packages.join(QStringLiteral(", ")).toStdString());
                    }
                    return false;
                }

                Config::instance().set_use_dxvk(false);
                SPDLOG_WARN("optional DXVK setup skipped because winetricks is unavailable");
                if (callbacks_.user_notice)
                {
                    callbacks_.user_notice(
                        QStringLiteral("Optional DXVK setup was skipped because "
                                       "Winetricks is unavailable. DXVK has been "
                                       "turned off and prefix setup will continue."));
                }
                return true;
            }

            if (proton && runtime_.proton_wine_binary().isEmpty())
            {
                SPDLOG_ERROR("selected Proton runtime does not expose a Wine binary for "
                             "winetricks");
                return false;
            }

            package_arguments << QStringLiteral("-q");
            package_arguments.append(packages);
            package_environment = runtime_.winetricks_environment();
        }

        int insertion_index = index_ + 1;
        if (!packages.isEmpty())
        {
            queue_.insert(insertion_index++,
                          SetupCommand {k_installing_message, installer, package_arguments,
                                        package_environment, 30 * 60 * 1000, false, false, false});
        }
        if (install_optional_dxvk)
        {
            queue_.insert(
                insertion_index,
                SetupCommand {QStringLiteral("Installing optional DXVK..."),
                              installer,
                              {QStringLiteral("-q"), PrefixInspector::dxvk_winetricks_verb()},
                              package_environment,
                              30 * 60 * 1000,
                              false,
                              false,
                              true});
        }
        return true;
    }

    void PrefixSetupJob::reset_state()
    {
        active_ = false;
        index_ = -1;
        queue_.clear();
        kind_ = Kind::None;
        marker_invalidated_ = false;
        ++generation_;
    }

    void PrefixSetupJob::finish_failure(const QString& message)
    {
        if (kind_ == Kind::Dxvk)
        {
            finish_optional_dxvk_failure(message);
            return;
        }

        reset_state();
        if (callbacks_.setup_status)
            callbacks_.setup_status(message);
        const QString title = QStringLiteral("Wine Setup Failed");
        if (callbacks_.fail_user)
            callbacks_.fail_user(title, message);
        if (callbacks_.setup_finished)
            callbacks_.setup_finished(false);
    }

    void PrefixSetupJob::finish_optional_dxvk_failure(const QString& message)
    {
        reset_state();
        Config::instance().set_use_dxvk(false);

        const QString user_message =
            message + QStringLiteral("\n\nDXVK has been turned off. The existing "
                                     "prefix remains usable with the built-in "
                                     "Direct3D backend.");
        SPDLOG_ERROR("optional DXVK setup failed: {}", message.toStdString());
        if (callbacks_.setup_status)
        {
            callbacks_.setup_status(QStringLiteral("DXVK could not be enabled; the prefix was "
                                                   "left unchanged."));
        }
        if (callbacks_.user_error)
        {
            callbacks_.user_error(QStringLiteral("DXVK Setup Failed"), user_message);
        }
        if (callbacks_.done)
        {
            callbacks_.done(QStringLiteral("DXVK is off; the existing prefix remains "
                                           "ready."));
        }
        if (callbacks_.setup_finished)
            callbacks_.setup_finished(true);
    }

    void PrefixSetupJob::skip_optional_command(const QString& message)
    {
        Config::instance().set_use_dxvk(false);
        SPDLOG_WARN("optional DXVK setup skipped: {}", message.toStdString());
        if (callbacks_.setup_status)
        {
            callbacks_.setup_status(QStringLiteral("Optional DXVK setup failed; continuing "
                                                   "prefix setup..."));
        }
        if (callbacks_.user_notice)
        {
            callbacks_.user_notice(QStringLiteral("Optional DXVK setup failed and DXVK was "
                                                  "turned off. Required prefix setup will "
                                                  "continue; details are in launcher.log."));
        }
        ++index_;
        advance();
    }

    void PrefixSetupJob::run_setup(QVector<SetupCommand> commands, const Kind kind)
    {
        queue_ = std::move(commands);
        index_ = 0;
        kind_ = kind;
        marker_invalidated_ = false;
        active_ = true;
        ++generation_;
        advance();
    }

    void PrefixSetupJob::advance()
    {
        if (!active_ || index_ < 0)
            return;
        if (index_ >= queue_.size())
        {
            index_ = -1;
            queue_.clear();
            const Kind completed_kind = kind_;
            finalize(completed_kind, k_prefix_validation_retries);
            return;
        }

        const SetupCommand command = queue_[index_];
        if (command.invalidates_marker && !marker_invalidated_)
        {
            if (!PrefixInspector::remove_marker(Config::instance().prefix_root()))
            {
                finish_failure(QStringLiteral("Could not invalidate the old prefix "
                                              "setup marker."));
                return;
            }
            marker_invalidated_ = true;
        }

        if (callbacks_.setup_status)
            callbacks_.setup_status(command.message);
        if (callbacks_.working)
        {
            callbacks_.working(QStringLiteral("prefix-setup"), -1.0, false);
        }

        const quint64 generation = generation_;
        ProcessRunner::Request request;
        request.program = command.program;
        request.arguments = command.arguments;
        request.environment = command.environment;
        request.timeout_ms = command.timeout_ms;
        if (!runner_.start(
                std::move(request),
                [this, generation, command](const command_result& result)
                {
                    if (callbacks_.command_finished)
                        callbacks_.command_finished(result);
                    if (!active_ || generation != generation_)
                        return;

                    if (!result.ok())
                    {
                        const QString message = command_failure_message(command.message, result);
                        if (command.succeeds_if_prefix_ready && prefix_structure_ready())
                        {
                            SPDLOG_INFO("prefix: \"{}\" exited non-zero but the prefix is "
                                        "initialised; continuing",
                                        command.message.toStdString());
                        }
                        else if (command.optional_failure)
                        {
                            skip_optional_command(message);
                            return;
                        }
                        else
                        {
                            finish_failure(message);
                            return;
                        }
                    }
                    if (command.inspect_components_after && !queue_missing_components())
                    {
                        finish_failure(QStringLiteral("Required components are missing, but "
                                                      "Winetricks could not install them."));
                        return;
                    }
                    ++index_;
                    advance();
                }))
        {
            if (command.optional_failure)
            {
                skip_optional_command(QStringLiteral("%1 could not be started. "
                                                     "See launcher.log for details.")
                                          .arg(command.message));
                return;
            }
            finish_failure(QStringLiteral("Could not start %1").arg(command.message));
        }
    }

    bool PrefixSetupJob::prefix_structure_ready() const
    {
        const QString runtime_identity = runtime_.runtime_is_proton()
                                             ? Config::instance().wine_binary()
                                             : runtime_.wine_binary();
        return PrefixInspector::inspect(Config::instance().prefix_root(), runtime_identity,
                                        runtime_.runtime_is_proton())
            .structure_valid;
    }

    void PrefixSetupJob::finalize(const Kind completed_kind, const int attempts_remaining)
    {
        if (!active_)
            return;

        const QString runtime_identity = runtime_.runtime_is_proton()
                                             ? Config::instance().wine_binary()
                                             : runtime_.wine_binary();
        const auto inspection = PrefixInspector::inspect(
            Config::instance().prefix_root(), runtime_identity, runtime_.runtime_is_proton());
        const bool dxvk_requested = Config::instance().use_dxvk() && !runtime_.runtime_is_proton();
        if (dxvk_requested && !inspection.dxvk_installed)
        {
            if (attempts_remaining > 0)
            {
                SPDLOG_DEBUG("DXVK validation pending: files_present={}, "
                             "overrides_present={}, retries_remaining={}",
                             inspection.dxvk_files_present, inspection.dxvk_overrides_present,
                             attempts_remaining);
                const quint64 generation = generation_;
                QTimer::singleShot(k_prefix_validation_retry_ms, this,
                                   [this, generation, completed_kind, attempts_remaining]()
                                   {
                                       if (active_ && generation == generation_)
                                       {
                                           finalize(completed_kind, attempts_remaining - 1);
                                       }
                                   });
                return;
            }

            SPDLOG_ERROR("DXVK validation failed: structure_valid={}, "
                         "files_present={}, overrides_present={}",
                         inspection.structure_valid, inspection.dxvk_files_present,
                         inspection.dxvk_overrides_present);
            const QString reason = !inspection.dxvk_files_present
                                       ? QStringLiteral("The 32-bit d3d9/dxgi files are missing.")
                                       : QStringLiteral("The DLL files are present, but their "
                                                        "native overrides did not become visible "
                                                        "in the persisted prefix registry.");

            if (completed_kind == Kind::Dxvk)
            {
                finish_optional_dxvk_failure(
                    QStringLiteral("Winetricks finished, but DXVK could "
                                   "not be verified. %1 Check "
                                   "launcher.log for the installer output.")
                        .arg(reason));
                return;
            }

            Config::instance().set_use_dxvk(false);
            SPDLOG_WARN("optional DXVK command completed without a "
                        "verifiable installation; DXVK was turned off");
            if (callbacks_.user_notice)
            {
                callbacks_.user_notice(QStringLiteral("DXVK could not be verified and was "
                                                      "turned off. %1 The prefix remains ready "
                                                      "with the built-in Direct3D backend.")
                                           .arg(reason));
            }
        }

        if (!inspection.required_components_present(runtime_.runtime_is_proton()))
        {
            if (attempts_remaining > 0)
            {
                const quint64 generation = generation_;
                QTimer::singleShot(k_prefix_validation_retry_ms, this,
                                   [this, generation, completed_kind, attempts_remaining]()
                                   {
                                       if (active_ && generation == generation_)
                                       {
                                           finalize(completed_kind, attempts_remaining - 1);
                                       }
                                   });
                return;
            }

            const char* architecture = "unknown";
            if (inspection.architecture == PrefixArchitecture::Win32)
            {
                architecture = "win32";
            }
            else if (inspection.architecture == PrefixArchitecture::Win64)
            {
                architecture = "win64";
            }
            SPDLOG_ERROR("prefix validation failed: exists={}, "
                         "structure_valid={}, architecture={}, "
                         "d3dx9_31={}, d3dx9_42={}, "
                         "d3dcompiler_42={}, d3dcompiler_47={}, "
                         "msvc2010_runtime={}, physx_runtime={}, "
                         "d3dx9_43={}, msvc_runtime={}, "
                         "dxvk_files_present={}, "
                         "dxvk_overrides_present={}, "
                         "dxvk_installed={}",
                         inspection.exists, inspection.structure_valid, architecture,
                         inspection.d3dx9_31, inspection.d3dx9_42, inspection.d3dcompiler_42,
                         inspection.d3dcompiler_47, inspection.msvc2010_runtime,
                         inspection.physx_runtime, inspection.d3dx9_43, inspection.msvc_runtime,
                         inspection.dxvk_files_present, inspection.dxvk_overrides_present,
                         inspection.dxvk_installed);
#if defined(Q_OS_MACOS)
            if (!inspection.exists || !inspection.structure_valid ||
                inspection.architecture == PrefixArchitecture::Win32)
            {
                finish_failure(QStringLiteral("Prefix setup finished, but the Wine prefix "
                                              "structure is incomplete or "
                                              "incompatible."));
            }
            else
            {
                finish_failure(QStringLiteral("Prefix setup finished, but Alicia's "
                                              "required DirectX or VC++ components are "
                                              "still missing."));
            }
#else
            if (completed_kind == Kind::Dxvk)
            {
                finish_optional_dxvk_failure(
                    QStringLiteral("DXVK setup finished, but the prefix is "
                                   "not ready. Run prefix setup before "
                                   "enabling DXVK."));
            }
            else
            {
                finish_failure(QStringLiteral("Prefix setup finished, but required "
                                              "components are still missing."));
            }
#endif
            return;
        }

        if (!PrefixInspector::write_marker(Config::instance().prefix_root(), runtime_identity))
        {
            const QString message = QStringLiteral("Setup completed, but the prefix could not be "
                                                   "marked ready.");
            if (completed_kind == Kind::Dxvk)
                finish_optional_dxvk_failure(message);
            else
                finish_failure(message);
            return;
        }

        reset_state();
        if (callbacks_.setup_status)
        {
            callbacks_.setup_status(QStringLiteral("Done!"));
        }
        if (callbacks_.done)
        {
            callbacks_.done(completed_kind == Kind::Dxvk
                                ? QStringLiteral("DXVK setup complete.")
                                : QStringLiteral("Prefix setup complete."));
        }
        if (callbacks_.setup_finished)
            callbacks_.setup_finished(true);
    }

    void PrefixSetupJob::setup_wine()
    {
        if (!ensure_idle())
            return;
#if defined(Q_OS_MACOS)
        const QString configured_runtime = Config::instance().wine_binary();
        const QString runtime_executable = macos::resolve_wine_executable(configured_runtime);
        if (runtime_executable.isEmpty() || !QFileInfo(runtime_executable).isExecutable())
        {
            if (callbacks_.fail_user)
            {
                callbacks_.fail_user(QStringLiteral("Wine Not Available"),
                                     QStringLiteral("No executable Wine entry point was "
                                                    "found in the selected app or folder."));
            }
            if (callbacks_.setup_finished)
                callbacks_.setup_finished(false);
            return;
        }
        if (macos::executable_requires_rosetta(runtime_executable) &&
            !macos::rosetta_is_available())
        {
            if (callbacks_.fail_user)
            {
                callbacks_.fail_user(QStringLiteral("Rosetta Required"),
                                     QStringLiteral("The selected Wine installation is Intel-only. "
                                                    "Request Rosetta in the Wine "
                                                    "chooser, complete the macOS prompt, "
                                                    "then retry."));
            }
            if (callbacks_.setup_finished)
                callbacks_.setup_finished(false);
            return;
        }
#else
        if (!runtime_.is_wine_installed())
        {
            if (callbacks_.fail_user)
            {
                callbacks_.fail_user(QStringLiteral("Wine Not Available"),
                                     QStringLiteral("The selected Wine runtime is missing "
                                                    "or not executable."));
            }
            if (callbacks_.setup_finished)
                callbacks_.setup_finished(false);
            return;
        }
#endif

        const QString prefix = Config::instance().prefix_root();
        if (prefix.isEmpty() || !QDir().mkpath(prefix))
        {
#if defined(Q_OS_MACOS)
            const QString prefix_message =
                QStringLiteral("The Wine prefix directory could not be "
                               "created.");
#else
            const QString prefix_message = QStringLiteral("The Wine prefix directory could not be "
                                                          "created.");
#endif
            if (callbacks_.fail_user)
            {
                callbacks_.fail_user(QStringLiteral("Invalid Prefix"), prefix_message);
            }
            if (callbacks_.setup_finished)
                callbacks_.setup_finished(false);
            return;
        }

        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("WINEPREFIX"), prefix);
#if defined(Q_OS_MACOS)
        environment.insert(QStringLiteral("WINEARCH"), QStringLiteral("win64"));
#else
        environment.insert(QStringLiteral("WINEARCH"), Config::instance().wine_arch());
#endif
        environment.insert(QStringLiteral("WINE"),
                           runtime_.resolved_executable(runtime_.wine_binary()));
        runtime_.apply_wine_environment(environment);

        QVector<SetupCommand> commands;
#if defined(Q_OS_MACOS)
        const QString validate_runtime_message = QStringLiteral("Validating Wine...");
        const QString create_prefix_message = QStringLiteral("Creating Wine prefix...");
#else
        const QString validate_runtime_message = QStringLiteral("Validating Wine runtime...");
        const QString create_prefix_message = QStringLiteral("Creating Wine prefix...");
#endif
        commands.push_back({validate_runtime_message,
                            runtime_.wine_binary(),
                            {QStringLiteral("--version")},
                            environment,
                            k_runtime_probe_timeout_ms,
                            false,
                            false,
                            false});

        const QString wineboot = runtime_.wineboot_binary();
        QStringList wineboot_arguments;
        if (QFileInfo(wineboot).canonicalFilePath() ==
            QFileInfo(runtime_.resolved_executable(runtime_.wine_binary())).canonicalFilePath())
        {
            wineboot_arguments << QStringLiteral("wineboot");
        }
        wineboot_arguments << QStringLiteral("--init");
        commands.push_back({create_prefix_message, wineboot, wineboot_arguments, environment,
                            5 * 60 * 1000, true, true, false});

#if defined(Q_OS_MACOS)
        const QString d3d_key = QStringLiteral("HKCU\\Software\\Wine\\Direct3D");
        commands.push_back({QStringLiteral("Clearing stale graphics overrides..."),
                            runtime_.wine_binary(),
                            {QStringLiteral("cmd.exe"), QStringLiteral("/d"), QStringLiteral("/s"),
                             QStringLiteral("/c"),
                             QStringLiteral("reg.exe delete \"%1\" /v renderer /f "
                                            ">nul 2>&1 & exit /b 0")
                                 .arg(d3d_key)},
                            environment,
                            k_registry_timeout_ms,
                            false,
                            true,
                            false});
        commands.push_back({QStringLiteral("Configuring the built-in graphics "
                                           "framebuffer..."),
                            runtime_.wine_binary(),
                            {QStringLiteral("reg.exe"), QStringLiteral("add"), d3d_key,
                             QStringLiteral("/v"), QStringLiteral("OffscreenRenderingMode"),
                             QStringLiteral("/t"), QStringLiteral("REG_SZ"), QStringLiteral("/d"),
                             QStringLiteral("fbo"), QStringLiteral("/f")},
                            environment,
                            k_registry_timeout_ms,
                            false,
                            true,
                            false});
        commands.push_back(
            {QStringLiteral("Configuring graphics memory..."),
             runtime_.wine_binary(),
             {QStringLiteral("reg.exe"), QStringLiteral("add"), d3d_key, QStringLiteral("/v"),
              QStringLiteral("VideoMemorySize"), QStringLiteral("/t"), QStringLiteral("REG_SZ"),
              QStringLiteral("/d"), QStringLiteral("4096"), QStringLiteral("/f")},
             environment,
             k_registry_timeout_ms,
             false,
             true,
             false});
#endif
        run_setup(std::move(commands), Kind::Setup);
    }

    void PrefixSetupJob::setup_proton()
    {
        if (!ensure_idle())
            return;
        if (!runtime_.is_wine_installed())
        {
            if (callbacks_.fail_user)
            {
                callbacks_.fail_user(QStringLiteral("Proton Not Available"),
                                     QStringLiteral("The selected Proton runtime is "
                                                    "missing or not executable."));
            }
            if (callbacks_.setup_finished)
                callbacks_.setup_finished(false);
            return;
        }

        const QString compat = Config::instance().proton_compat_data_root();
        if (compat.isEmpty() || !QDir().mkpath(compat))
        {
            if (callbacks_.fail_user)
            {
                callbacks_.fail_user(QStringLiteral("Invalid Compatibility Data Path"),
                                     QStringLiteral("The Proton compatibility-data "
                                                    "directory could not be created."));
            }
            if (callbacks_.setup_finished)
                callbacks_.setup_finished(false);
            return;
        }

        const QString umu = umu_path();
        if (umu.isEmpty() || !QFileInfo(umu).isExecutable())
        {
            if (callbacks_.fail_user)
            {
                callbacks_.fail_user(QStringLiteral("Proton Not Available"),
                                     QStringLiteral("Proton launch requires a working umu-run "
                                                    "installation."));
            }
            if (callbacks_.setup_finished)
                callbacks_.setup_finished(false);
            return;
        }

        QProcessEnvironment environment = runtime_.umu_environment();





        repair_doubled_proton_prefix(Config::instance().proton_compat_data_root());

        QVector<SetupCommand> commands;
        commands.push_back(
            {QStringLiteral("Creating Proton prefix..."),
             umu,




             {QStringLiteral("createprefix")},
             environment,
             5 * 60 * 1000,
             true,
             true,
             false,
             true});
        run_setup(std::move(commands), Kind::Setup);
    }

    void PrefixSetupJob::sync_dxvk()
    {
        if (!ensure_idle())
            return;
#if defined(Q_OS_MACOS)
        Config::instance().set_use_dxvk(false);
        if (callbacks_.user_notice)
        {
            callbacks_.user_notice(QStringLiteral("macOS uses Wine's built-in Direct3D "
                                                  "9 backend in this version. DXVK is "
                                                  "intentionally unavailable until a tested "
                                                  "Metal/Vulkan path exists."));
        }
        return;
#endif
        if (!Config::instance().use_dxvk())
        {
            if (callbacks_.user_notice)
            {
                callbacks_.user_notice(QStringLiteral("DXVK is disabled; WineD3D will be used."));
            }
            return;
        }
        if (runtime_.runtime_is_proton())
        {
            if (callbacks_.user_notice)
            {
                callbacks_.user_notice(QStringLiteral("DXVK is enabled. Proton provides it "
                                                      "internally, so no separate installation "
                                                      "is needed."));
            }
            return;
        }

        const QString prefix = Config::instance().prefix_root();
        const PrefixInspection inspection =
            PrefixInspector::inspect(prefix, runtime_.wine_binary(), false);
        if (!inspection.marker_valid || !inspection.required_components_present(false))
        {
            kind_ = Kind::Dxvk;
            active_ = true;
            finish_optional_dxvk_failure(
                QStringLiteral("The prefix is not ready for an optional "
                               "graphics-layer change. Create or repair the "
                               "prefix first."));
            return;
        }
        if (inspection.dxvk_installed)
        {
            if (callbacks_.user_notice)
            {
                callbacks_.user_notice(QStringLiteral("DXVK is already installed in this "
                                                      "prefix."));
            }
            return;
        }

        const QString winetricks = winetricks_path();
        if (winetricks.isEmpty() || !QFileInfo(winetricks).isExecutable())
        {
            kind_ = Kind::Dxvk;
            active_ = true;
            finish_optional_dxvk_failure(QStringLiteral("DXVK cannot be installed because "
                                                        "Winetricks is unavailable."));
            return;
        }

        QVector<SetupCommand> commands;
        commands.push_back({QStringLiteral("Installing DXVK..."),
                            winetricks,
                            {QStringLiteral("-q"), PrefixInspector::dxvk_winetricks_verb()},
                            runtime_.winetricks_environment(),
                            30 * 60 * 1000,
                            false,
                            false,
                            false});
        run_setup(std::move(commands), Kind::Dxvk);
    }

    void PrefixSetupJob::cancel()
    {
        if (!active_)
            return;
        if (runner_.is_busy())
        {
            runner_.cancel();
            return;
        }
        finish_failure(QStringLiteral("Prefix setup was cancelled."));
    }
}
