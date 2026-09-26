#include "app/MainWindow.hpp"
#include "MainWindowPrivate.hpp"

#include "config/Config.hpp"
#include "runtime/GameIntegrityWatcher.hpp"
#include "runtime/Shell.hpp"
#include "ui/AliciaChooser.hpp"
#include "ui/Assets.hpp"
#include "ui/GameInstall.hpp"
#include "ui/WineSelectMenu.hpp"
#include "ui/RulesAgreement.hpp"
#include "ui/PrerequisitesIntro.hpp"
#include "ui/DownloadProgress.hpp"
#include "ui/InstallState.hpp"
#include "ui/ViewRouter.hpp"
#include "ui/Layout.hpp"
#include "ui/ModalOverlay.hpp"
#include "ui/Settings.hpp"
#include "ui/WineInstall.hpp"

#include <QIcon>
#include <QPushButton>

using soa::common::game::GameVersion;
using soa::ui::Stage;
using soa::ui::View;
using soa::config::Config;

#ifndef SOA_LAUNCHER_VERSION
#define SOA_LAUNCHER_VERSION "0.3.0"
#endif

void MainWindow::continue_after_launcher_update_check()
{
    if (launcher_update_check_complete)
        return;

    launcher_update_check_complete = true;
    if (launch_after_preflight_check)
        install_state->recheck_before_launch();
}

void MainWindow::set_game_version(const GameVersion version)
{
    if (game_version == version) return;

    game_version = version;
    Config::instance().set_game_version(version);
    alicia_chooser->set_game_version(version);
    game_install->refresh_game_path();
    refresh_game_selector();
    install_state->probe();
    update();
}

void MainWindow::refresh_game_selector()
{
    const QSize window_size = size();
    const QRect playtest_rect = soa::ui::layout::chrome::playtest_button(window_size);
    const QRect alicia_2_rect = soa::ui::layout::chrome::alicia_2_button(window_size);

    const QPixmap& active = soa::ui::assets::images[soa::ui::assets::Image::VersionFrameActive];
    const QPixmap& inactive = soa::ui::assets::images[soa::ui::assets::Image::VersionFrameInactive];
    const QPixmap& playtest_icon = soa::ui::assets::images[soa::ui::assets::Image::VersionIconPlaytest];
    const QPixmap& alicia_2_icon = soa::ui::assets::images[soa::ui::assets::Image::VersionIconAlicia2];

    const QPixmap playtest_pixmap = soa::app::detail::make_version_button(
        window_size,
        playtest_rect,
        game_version == GameVersion::Playtest ? active : inactive,
        playtest_icon,
        soa::ui::layout::chrome::playtest_icon_offset(window_size));

    const QPixmap alicia_2_pixmap = soa::app::detail::make_version_button(
        window_size,
        alicia_2_rect,
        game_version == GameVersion::Alicia2 ? active : inactive,
        alicia_2_icon,
        soa::ui::layout::chrome::alicia_2_icon_offset(window_size));

    QIcon playtest_icon_set;
    playtest_icon_set.addPixmap(playtest_pixmap, QIcon::Normal);
    playtest_icon_set.addPixmap(playtest_pixmap, QIcon::Disabled);

    QIcon alicia_2_icon_set;
    alicia_2_icon_set.addPixmap(alicia_2_pixmap, QIcon::Normal);
    alicia_2_icon_set.addPixmap(alicia_2_pixmap, QIcon::Disabled);

    playtest_button->setIcon(playtest_icon_set);
    playtest_button->setIconSize(playtest_rect.size());
    alicia_2_button->setIcon(alicia_2_icon_set);
    alicia_2_button->setIconSize(alicia_2_rect.size());
}

void MainWindow::set_game_switching_enabled(const Stage stage)
{
    const bool enabled =
        !repair_active &&
        stage != Stage::SettingUpPrefix &&
        stage != Stage::Downloading &&
        stage != Stage::Updating &&
        stage != Stage::Authenticating &&
        stage != Stage::CheckingUpdate &&
        stage != Stage::Launching &&
        stage != Stage::Running;

    playtest_button->setEnabled(enabled);
    alicia_2_button->setEnabled(enabled);
}

void MainWindow::open_for_current_stage()
{
    const View view = soa::ui::view_for(install_state->stage());

    if (view == View::Prerequisites) open_overlay(prerequisites_intro);
    else if (view == View::WineSelect) open_overlay(wine_select);
    else if (view == View::WineInstall)
    {
        wine_install->refresh_prefix_path();
        open_overlay(wine_install);
    }
    else if (view == View::GameInstall)
    {
        game_install->refresh_game_path();
        open_overlay(game_install);
    }
    else if (install_state->stage() == Stage::NeedsUpdate && update_progress)
    {
        open_overlay(update_progress);
        update_progress->start_download();
    }
    else if (view == View::Rules) open_overlay(rules_agreement);
}

void MainWindow::on_stage_changed(const Stage stage)
{
    set_game_switching_enabled(stage);

    if (integrity_watcher)
    {
        const bool files_changing = repair_active
            || stage == Stage::Downloading
            || stage == Stage::Updating;
        integrity_watcher->set_suspended(files_changing);
    }

    if (launch_after_preflight_check)
    {
        if (stage == Stage::Ready)
        {
            launch_after_preflight_check = false;
            if (update_progress && update_progress->isVisible())
                close_overlay(update_progress);
            shell->run_game(Config::instance().username(), Config::instance().token());
            refresh_tray_actions();
            return;
        }
        if (stage != Stage::CheckingUpdate && stage != Stage::Updating)
        {
            launch_after_preflight_check = false;
            show_launcher();
        }
    }

    if (stage == Stage::Ready && update_progress
        && update_progress->isVisible() && update_progress->observing_operation())
    {
        close_overlay(update_progress);
    }

    const bool settingsEditable = !repair_active
        && stage != Stage::SettingUpPrefix
        && stage != Stage::Downloading
        && stage != Stage::Updating
        && stage != Stage::CheckingUpdate
        && stage != Stage::Authenticating
        && stage != Stage::Launching
        && stage != Stage::Running
        && !(shell && shell->is_busy());
    if (settings)
    {
        const bool game_active = stage == Stage::Launching
            || stage == Stage::Running
            || (shell && shell->is_game_running());
        settings->set_mutation_enabled(
            settingsEditable,
            game_active
                ? QStringLiteral("Disabled while Alicia is running")
                : QStringLiteral(
                    "Settings are read-only while Alicia or another launcher operation is active."));
    }

    if (repair_active && repair_progress && repair_progress->isVisible())
        return;

    const View view = soa::ui::view_for(stage);
    if (view == last_view) return;
    last_view = view;

    switch (view)
    {
        case View::Prerequisites:
            close_overlay(wine_select);
            close_overlay(wine_install);
            close_overlay(game_install);
            close_overlay(rules_agreement);
            open_overlay(prerequisites_intro);
            break;

        case View::WineSelect:
            close_overlay(prerequisites_intro);
            close_overlay(wine_install);
            close_overlay(game_install);
            close_overlay(rules_agreement);
            break;

        case View::WineInstall:
            close_overlay(prerequisites_intro);
            close_overlay(wine_select);
            close_overlay(game_install);
            close_overlay(rules_agreement);
            if (stage == Stage::NeedsPrefix) break;
            wine_install->refresh_prefix_path();
            open_overlay(wine_install);
            break;

        case View::GameInstall:
            close_overlay(prerequisites_intro);
            close_overlay(wine_select);
            close_overlay(wine_install);
            close_overlay(rules_agreement);
            game_install->refresh_game_path();
            open_overlay(game_install);
            break;

        case View::Rules:
            close_overlay(prerequisites_intro);
            close_overlay(wine_select);
            close_overlay(wine_install);
            close_overlay(game_install);
            open_overlay(rules_agreement);
            break;

        case View::AliciaChooser:
            close_overlay(prerequisites_intro);
            close_overlay(rules_agreement);
            close_overlay(wine_select);
            close_overlay(wine_install);
            close_overlay(game_install);
            break;

        case View::Loading:
        case View::Error:
            break;
    }
}

void MainWindow::update_chrome_visibility()
{
    bool should_hide = false;
    const auto overlays = findChildren<soa::ui::ModalOverlay*>(
        QString(), Qt::FindDirectChildrenOnly);
    for (const auto* overlay : overlays)
    {
        if (overlay->isVisible() && !overlay->keeps_chrome())
        {
            should_hide = true;
            break;
        }
    }

    chrome_hidden = should_hide;
    close_button->setVisible(!chrome_hidden);
    minimize_button->setVisible(!chrome_hidden);
    raise_persistent_controls();
    update();
}

void MainWindow::on_overlay_opened(soa::ui::ModalOverlay*)
{
    update_chrome_visibility();
}

void MainWindow::on_overlay_closed(soa::ui::ModalOverlay*)
{
    update_chrome_visibility();
}

void MainWindow::open_overlay(soa::ui::ModalOverlay* overlay)
{
    if (overlay->isHidden())
    {
        overlay->show_over(this);
        on_overlay_opened(overlay);
    }
    overlay->raise();
    raise_persistent_controls();
}

void MainWindow::close_overlay(soa::ui::ModalOverlay* overlay)
{
    if (!overlay->isHidden())
    {
        overlay->hide();
        on_overlay_closed(overlay);
        raise_persistent_controls();
    }
}

