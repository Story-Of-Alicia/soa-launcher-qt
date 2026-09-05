#include "app/MainWindow.hpp"
#include "app/LauncherMenuController.hpp"
#include "app/SystemTrayController.hpp"

#include "runtime/Shell.hpp"
#include "ui/Assets.hpp"
#include "ui/InstallState.hpp"
#include "ui/LauncherDialog.hpp"
#include "ui/LauncherUpdate.hpp"
#include "ui/Layout.hpp"
#include "ui/ModalOverlay.hpp"

#include <QCloseEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWindow>

using core::game::GameVersion;
using core::state::Stage;
using core::state::View;

#ifndef SOA_LAUNCHER_VERSION
#define SOA_LAUNCHER_VERSION "0.3.0"
#endif

void MainWindow::paintEvent(QPaintEvent*)
{
    const QSize window_size = size();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const QRect background_rect = util::layout::region::rect(window_size);
    const int radius = util::layout::scaled(util::layout::region::k_radius, window_size);
    QPainterPath path;
    path.addRoundedRect(background_rect, radius, radius);
    painter.setClipPath(path);

    const util::assets::Image background = game_version == GameVersion::Alicia2
        ? util::assets::Image::BackgroundAlicia2
        : util::assets::Image::BackgroundPlaytest;
    painter.drawPixmap(background_rect, util::assets::images[background]);
    painter.setClipping(false);

    if (!chrome_hidden)
    {
        const QPixmap left = util::assets::images[util::assets::Image::LeftFrame]
            .scaledToHeight(height(), Qt::SmoothTransformation);
        painter.drawPixmap(0, 0, left);

        const QPixmap right = util::assets::images[util::assets::Image::RightFrame]
            .scaledToHeight(height(), Qt::SmoothTransformation);
        painter.drawPixmap(width() - right.width(), 0, right);
    }
}

void MainWindow::mousePressEvent(QMouseEvent* event)
{
    if (launcher_menu_controller && launcher_menu_controller->is_visible()
        && !launcher_menu_controller->contains(event->position().toPoint()))
    {
        launcher_menu_controller->set_visible(false);
    }

    const int drag_height = util::layout::scaled(58, size());
    if (event->button() == Qt::LeftButton && event->position().y() <= drag_height
        && windowHandle())
    {
        windowHandle()->startSystemMove();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (force_quit_requested)
    {
        event->accept();
        return;
    }

    if (tray_controller && tray_controller->is_visible())
    {
        if (launcher_menu_controller)
            launcher_menu_controller->set_visible(false);
        hide();
        event->ignore();
        return;
    }

    const bool gameRunning = shell && shell->is_game_running();
    const bool operationActive = gameRunning
        || repair_active
        || (launcher_update && launcher_update->busy())
        || (shell && shell->is_busy())
        || (install_state && (install_state->stage() == core::state::Stage::Downloading
            || install_state->stage() == core::state::Stage::Updating
            || install_state->stage() == core::state::Stage::SettingUpPrefix
            || install_state->stage() == core::state::Stage::CheckingUpdate
            || install_state->stage() == core::state::Stage::Authenticating
            || install_state->stage() == core::state::Stage::Launching));

    if (!operationActive)
    {
        event->accept();
        return;
    }

    QVector<LauncherDialog::Action> actions;
    if (gameRunning)
    {
        actions.push_back({QStringLiteral("Minimize Launcher"),
                           LauncherDialog::Primary,
                           LauncherDialog::ActionStyle::Primary,
                           true});
    }
    actions.push_back({QStringLiteral("Cancel"),
                       LauncherDialog::Cancelled,
                       LauncherDialog::ActionStyle::Neutral,
                       !gameRunning});
    actions.push_back({QStringLiteral("Close Anyway"),
                       LauncherDialog::Secondary,
                       LauncherDialog::ActionStyle::Destructive,
                       false});

    const int result = LauncherDialog::choose(
        this,
        LauncherDialog::Tone::Warning,
        gameRunning ? QStringLiteral("Alicia Is Running")
                    : QStringLiteral("Operation In Progress"),
        gameRunning
            ? QStringLiteral("Closing the launcher stops live diagnostics and process monitoring. Alicia may continue running and will be detected again when the launcher restarts.")
            : QStringLiteral("Closing now may interrupt setup, authentication, download, repair, or update work."),
        actions);

    if (gameRunning && result == LauncherDialog::Primary)
    {
        showMinimized();
        event->ignore();
    }
    else if (result == LauncherDialog::Secondary)
    {
        event->accept();
    }
    else
    {
        event->ignore();
    }
}

