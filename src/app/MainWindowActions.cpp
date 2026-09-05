#include "app/MainWindow.hpp"
#include "app/LauncherMenuController.hpp"
#include "app/SystemTrayController.hpp"

#include "config/Config.hpp"
#include "i18n/LanguageManager.hpp"
#include "runtime/Shell.hpp"
#include "ui/InstallState.hpp"
#include "ui/LauncherDialog.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

using core::game::GameVersion;
using core::state::Stage;
using core::state::View;
using util::config::Config;

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

    shell->run_game(Config::instance().username(), Config::instance().token());
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
        close_button->setAccessibleName(util::i18n::translate("Close launcher"));
    if (minimize_button)
        minimize_button->setAccessibleName(util::i18n::translate("Minimize launcher"));
    if (version_label)
        version_label->setText(util::i18n::translate("VERSION") + QLatin1Char(' ')
            + QString::fromLatin1(SOA_LAUNCHER_VERSION));
}

