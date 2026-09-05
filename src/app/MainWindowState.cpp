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

using core::game::GameVersion;
using core::state::Stage;
using core::state::View;
using util::config::Config;

#ifndef SOA_LAUNCHER_VERSION
#define SOA_LAUNCHER_VERSION "0.3.0"
#endif

void MainWindow::continue_after_launcher_update_check()
{
    if (launcher_update_check_complete)
        return;
    launcher_update_check_complete = true;
    install_state->probe();
    shell->detect_existing_game();
    refresh_tray_actions();
    raise_persistent_controls();
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
    const QRect playtest_rect = util::layout::chrome::playtest_button(window_size);
    const QRect alicia_2_rect = util::layout::chrome::alicia_2_button(window_size);

    const QPixmap& active = util::assets::images[util::assets::Image::VersionFrameActive];
    const QPixmap& inactive = util::assets::images[util::assets::Image::VersionFrameInactive];
    const QPixmap& playtest_icon = util::assets::images[util::assets::Image::VersionIconPlaytest];
    const QPixmap& alicia_2_icon = util::assets::images[util::assets::Image::VersionIconAlicia2];

    const QPixmap playtest_pixmap = app::detail::make_version_button(
        window_size,
        playtest_rect,
        game_version == GameVersion::Playtest ? active : inactive,
        playtest_icon,
        util::layout::chrome::playtest_icon_offset(window_size));

    const QPixmap alicia_2_pixmap = app::detail::make_version_button(
        window_size,
        alicia_2_rect,
        game_version == GameVersion::Alicia2 ? active : inactive,
        alicia_2_icon,
        util::layout::chrome::alicia_2_icon_offset(window_size));

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
    const View view = core::state::view_for(install_state->stage());

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
    else if (view == View::Rules) open_overlay(rules_agreement);
}

void MainWindow::on_stage_changed(const Stage stage)
{
    set_game_switching_enabled(stage);

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

    if (integrity_watcher)
    {
        const bool files_changing = repair_active
            || stage == Stage::Downloading
            || stage == Stage::Updating
            || stage == Stage::Launching
            || stage == Stage::Running;
        integrity_watcher->set_suspended(files_changing);
    }

    if (repair_active && repair_progress && repair_progress->isVisible())
        return;

    const View view = core::state::view_for(stage);
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
    const auto overlays = findChildren<util::modal_overlay::ModalOverlay*>(
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

void MainWindow::on_overlay_opened(util::modal_overlay::ModalOverlay*)
{
    update_chrome_visibility();
}

void MainWindow::on_overlay_closed(util::modal_overlay::ModalOverlay*)
{
    update_chrome_visibility();
}

void MainWindow::open_overlay(util::modal_overlay::ModalOverlay* overlay)
{
    if (!overlay->isVisible())
    {
        overlay->show_over(this);
        on_overlay_opened(overlay);
        raise_persistent_controls();
    }
}

void MainWindow::close_overlay(util::modal_overlay::ModalOverlay* overlay)
{
    if (overlay->isVisible())
    {
        overlay->hide();
        on_overlay_closed(overlay);
        raise_persistent_controls();
    }
}

