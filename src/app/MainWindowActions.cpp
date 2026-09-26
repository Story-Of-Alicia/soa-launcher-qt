#include "app/MainWindow.hpp"
#include "app/LauncherMenuController.hpp"
#include "app/SystemTrayController.hpp"

#include "config/Config.hpp"
#include "i18n/LanguageManager.hpp"
#include "runtime/Shell.hpp"
#include "update/LauncherUpdateManager.hpp"
#include "ui/InstallState.hpp"
#include "ui/RepairFiles.hpp"
#include "ui/LauncherDialog.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

using soa::common::game::GameVersion;
using soa::ui::Stage;
using soa::ui::View;
using soa::config::Config;

#ifndef SOA_LAUNCHER_VERSION
#define SOA_LAUNCHER_VERSION "0.3.0"
#endif

void MainWindow::show_launcher()
{
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    show();
    raise();
    activateWindow();
    raise_persistent_controls();
}

bool MainWindow::can_run_game_directly() const
{
    const auto& config = Config::instance();
    return shell
        && install_state
        && install_state->stage() == Stage::Ready
        && !shell->is_busy()
        && config.has_auth()
        && config.game_installed()
        && config.path_inside_prefix(config.game_install_path())
        && shell->is_wine_installed();
}

void MainWindow::request_game_launch()
{
    if (!can_run_game_directly())
        return;

    const int repair_key = static_cast<int>(game_version);
    if (repair_files && pending_integrity_repairs.contains(repair_key))
    {
        repair_files->set_game_version(game_version);
        repair_files->set_detected_changes(pending_integrity_repairs.value(repair_key));
        open_overlay(repair_files);
        show_launcher();
        return;
    }

    launch_after_preflight_check = true;
    if (!launcher_update_check_complete && launcher_update_manager)
    {
        launcher_update_manager->check_for_updates();
        return;
    }

    install_state->recheck_before_launch();
}

void MainWindow::run_game_directly()
{
    if (!can_run_game_directly())
    {
        show_launcher();
        LauncherDialog::information(
            this,
            QStringLiteral("Alicia Is Not Ready"),
            QStringLiteral(
                "Finish setup, install the selected game, and sign in before launching Alicia directly."));
        return;
    }

    request_game_launch();
    refresh_tray_actions();
}

void MainWindow::refresh_tray_actions()
{
    if (tray_controller)
        tray_controller->set_run_enabled(can_run_game_directly());
}

void MainWindow::raise_persistent_controls()
{
    if (launcher_menu_controller)
        launcher_menu_controller->raise_controls(chrome_hidden);
    if (!chrome_hidden)
    {
        if (close_button)
            close_button->raise();
        if (minimize_button)
            minimize_button->raise();
    }
}

void MainWindow::request_quit()
{
    force_quit_requested = true;
    if (launcher_menu_controller)
        launcher_menu_controller->set_visible(false);
    if (tray_controller)
        tray_controller->hide();
    close();
    QTimer::singleShot(0, qApp, []()
    {
        QCoreApplication::exit(0);
    });
}

void MainWindow::retranslate_dynamic_text()
{
    if (launcher_menu_controller)
        launcher_menu_controller->retranslate();
    if (tray_controller)
        tray_controller->retranslate();
    if (close_button)
        close_button->setAccessibleName(soa::i18n::translate("Close launcher"));
    if (minimize_button)
        minimize_button->setAccessibleName(soa::i18n::translate("Minimize launcher"));
    if (version_label)
        version_label->setText(soa::i18n::translate("VERSION") + QLatin1Char(' ')
            + QString::fromLatin1(SOA_LAUNCHER_VERSION));
}

