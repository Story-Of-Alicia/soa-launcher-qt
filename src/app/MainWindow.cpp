#include "app/MainWindow.hpp"
#include "MainWindowPrivate.hpp"
#include "app/LauncherMenuController.hpp"
#include "app/SystemTrayController.hpp"

#include "auth/AuthHandler.hpp"
#include "network/DiscordRpc.hpp"
#include "runtime/Shell.hpp"
#include "update/LauncherUpdateManager.hpp"
#include "ui/InstallState.hpp"
#include "ui/DownloadProgress.hpp"
#include "ui/GameInstall.hpp"
#include "ui/LauncherDialog.hpp"
#include "ui/LauncherInfoDialog.hpp"
#include "ui/LauncherLog.hpp"
#include "i18n/LanguageManager.hpp"

#include <QDialog>
#include <QTimer>

using core::game::GameVersion;
using core::state::Stage;
using core::state::View;
using util::config::Config;

#ifndef SOA_LAUNCHER_VERSION
#define SOA_LAUNCHER_VERSION "0.3.0"
#endif

MainWindow::MainWindow(QWidget* parent)
    : QWidget(parent),
      game_version(Config::instance().game_version())
{
    setWindowFlags(Qt::FramelessWindowHint);
    setFixedSize(app::detail::configured_window_size());
    setAttribute(Qt::WA_TranslucentBackground);

    shell = new core::wine::Shell(this);
    auth = new AuthHandler(this);
    install_state = new core::state::InstallState(this);
    connect(auth, &AuthHandler::browser_open_failed, this, [this]()
    {
        LauncherDialog::warning(
            this,
            QStringLiteral("Browser Could Not Be Opened"),
            QStringLiteral(
                "The default browser could not be opened. The exact sign-in link was copied "
                "to your clipboard instead."));
    });

    setup_discord_rpc();
    setup_system_tray();
    setup_window_buttons();
    setup_launcher_menu();
    setup_version_label();
    setup_settings();
    setup_alicia_chooser();
    setup_prerequisites();
    setup_rules();
    setup_repair_files();
    setup_integrity_watcher();
    setup_game_selector();
    setup_wine_install();
    setup_game_install();
    setup_wine_select();
    setup_launcher_updates();

    connect(install_state, &core::state::InstallState::stage_changed,
            this, &MainWindow::on_stage_changed);
    connect(install_state, &core::state::InstallState::stage_changed,
            this, [this]() { refresh_tray_actions(); });
    connect(&Config::instance(), &Config::changed,
            this, [this]() { refresh_tray_actions(); });
    connect(install_state, &core::state::InstallState::error_changed, this,
            [this](const QString& message)
    {
        if (message.isEmpty())
            return;
        if ((game_install && game_install->isVisible())
            || (repair_progress && repair_progress->isVisible()))
            return;
        LauncherDialog* box = app::detail::show_modeless_message(
            this, LauncherDialog::Tone::Error, QStringLiteral("Launcher Error"), message,
            QStringLiteral(
                "The launcher log has diagnostic details. Close this message, then retry the action."));
        connect(box, &QDialog::finished, install_state,
                [this]() { install_state->dismiss_error(); });
    });
    connect(shell, &core::wine::Shell::user_notice, this, [this](const QString& message)
    {
        app::detail::show_modeless_message(this, LauncherDialog::Tone::Information,
                               QStringLiteral("Story of Alicia Launcher"), message);
    });
    connect(&Config::instance(), &Config::persistence_failed, this,
            [this](const QString& path, const QString& reason)
    {
        app::detail::show_modeless_message(
            this, LauncherDialog::Tone::Error, QStringLiteral("Settings Not Saved"),
            util::i18n::translate(
                "The launcher could not save config.json.\n\nPath: %1\nReason: %2\n\n"
                "Your on-screen change is active only for this session.")
                .arg(path, reason));
    });
    if (!Config::instance().persistence_error().isEmpty())
    {
        app::detail::show_modeless_message(
            this, LauncherDialog::Tone::Error, QStringLiteral("Settings Not Saved"),
            util::i18n::translate(
                "The launcher could not initialize config.json.\n\nPath: %1\n"
                "Reason: %2")
                .arg(Config::instance().file_path(),
                     Config::instance().persistence_error()));
    }
    connect(shell, &core::wine::Shell::game_started, this, [this](core::game::GameVersion)
    {
        const QString proxy_username = Config::instance().display_name().trimmed().isEmpty()
            ? Config::instance().username()
            : Config::instance().display_name();
        if (discord_rpc)
            discord_rpc->set_game_presence(proxy_username);
        refresh_tray_actions();
        if (Config::instance().after_game_start() == QStringLiteral("minimize"))
        {
            minimized_for_game = true;
            showMinimized();
        }
    });
    connect(shell, &core::wine::Shell::game_exited, this,
            [this](core::game::GameVersion, int, bool)
    {
        if (discord_rpc)
            discord_rpc->set_launcher_presence();
        refresh_tray_actions();
        if (!minimized_for_game)
            return;
        minimized_for_game = false;
        show_launcher();
    });

    refresh_tray_actions();
    raise_persistent_controls();
    QTimer::singleShot(0, launcher_update_manager,
                       &core::update::LauncherUpdateManager::check_for_updates);

    connect(&util::i18n::LanguageManager::instance(),
            &util::i18n::LanguageManager::language_changed, this,
            [this]()
    {
        retranslate_dynamic_text();
        raise_persistent_controls();
    });
}

MainWindow::~MainWindow()
{
    if (tray_controller)
        tray_controller->hide();
    if (discord_rpc)
        discord_rpc->shutdown();
}

void MainWindow::setup_discord_rpc()
{
    discord_rpc = new core::discord::DiscordRpc(this);
    discord_rpc->set_launcher_presence();
}

void MainWindow::setup_system_tray()
{
    tray_controller = new SystemTrayController(this);
    connect(tray_controller, &SystemTrayController::open_requested,
            this, &MainWindow::show_launcher);
    connect(tray_controller, &SystemTrayController::run_requested,
            this, &MainWindow::run_game_directly);
    connect(tray_controller, &SystemTrayController::quit_requested,
            this, &MainWindow::request_quit);
    connect(tray_controller, &SystemTrayController::about_to_show,
            this, &MainWindow::refresh_tray_actions);
}

bool MainWindow::has_system_tray() const
{
    return tray_controller && tray_controller->is_visible();
}

void MainWindow::setup_launcher_menu()
{
    launcher_menu_controller = new LauncherMenuController(this, this);
    connect(launcher_menu_controller, &LauncherMenuController::show_log_requested,
            this, []()
    {
        LauncherLog* log = LauncherLog::instance();
        log->showNormal();
        log->raise();
        log->activateWindow();
    });
    connect(launcher_menu_controller, &LauncherMenuController::manage_versions_requested,
            this, [this]()
    {
        if (!launcher_update_manager)
        {
            launcher_menu_controller->set_manage_versions_enabled(true);
            return;
        }
        launcher_update_manager->manage_versions();
    });
    connect(launcher_menu_controller, &LauncherMenuController::about_requested,
            this, &MainWindow::show_about);
    connect(launcher_menu_controller, &LauncherMenuController::credits_requested,
            this, &MainWindow::show_credits);
}

void MainWindow::show_about()
{
    auto* dialog = new LauncherInfoDialog(LauncherInfoDialog::Page::About, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

void MainWindow::show_credits()
{
    auto* dialog = new LauncherInfoDialog(LauncherInfoDialog::Page::Credits, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

