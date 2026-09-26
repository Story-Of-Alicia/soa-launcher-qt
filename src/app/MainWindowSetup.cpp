#include "app/MainWindow.hpp"
#include "MainWindowPrivate.hpp"
#include "app/LauncherMenuController.hpp"

#include "auth/AuthHandler.hpp"
#include "config/Config.hpp"
#include "i18n/LanguageManager.hpp"
#include "runtime/GameIntegrityWatcher.hpp"
#include "runtime/Shell.hpp"
#include "update/LauncherUpdateManager.hpp"
#include "ui/AliciaChooser.hpp"
#include "ui/Assets.hpp"
#include "ui/DownloadProgress.hpp"
#include "ui/GameInstall.hpp"
#include "ui/InstallState.hpp"
#include "ui/LauncherDialog.hpp"
#include "ui/LauncherUpdate.hpp"
#include "ui/Layout.hpp"
#include "ui/PrerequisitesIntro.hpp"
#include "ui/RepairFiles.hpp"
#include "ui/RulesAgreement.hpp"
#include "ui/Settings.hpp"
#include "ui/SimpleUtils.hpp"
#include "ui/WineInstall.hpp"
#include "ui/WineSelectMenu.hpp"

#include <QIcon>
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

void MainWindow::setup_window_buttons()
{
    const QSize window_size = size();

    close_button = soa::ui::simple_utils::make_flat_button(this);
    close_button->setIcon(QIcon(soa::ui::assets::images[soa::ui::assets::Image::CloseIcon]));
    close_button->setIconSize(soa::ui::layout::chrome::close_icon(window_size));
    close_button->setGeometry(soa::ui::layout::chrome::close(window_size));
    close_button->setAccessibleName(QStringLiteral("Close launcher"));
    connect(close_button, &QPushButton::clicked, this, [this]()
    {
        close();
    });

    minimize_button = soa::ui::simple_utils::make_flat_button(this);
    minimize_button->setIcon(QIcon(soa::ui::assets::images[soa::ui::assets::Image::Minimize]));
    minimize_button->setIconSize(soa::ui::layout::chrome::minimize_icon(window_size));
    minimize_button->setGeometry(soa::ui::layout::chrome::minimize(window_size));
    minimize_button->setAccessibleName(QStringLiteral("Minimize launcher"));
    connect(minimize_button, &QPushButton::clicked, this, [this]()
    {
        showMinimized();
    });
}

void MainWindow::setup_version_label()
{
    const QSize window_size = size();

    version_art_label = new QLabel(this);
    version_art_label->setAttribute(Qt::WA_TransparentForMouseEvents);
    version_art_label->setAlignment(Qt::AlignRight | Qt::AlignBottom);
    const QRect art_rect = soa::ui::layout::chrome::version_art(window_size);
    version_art_label->setGeometry(art_rect);
    version_art_label->setPixmap(soa::ui::assets::images[soa::ui::assets::Image::VersionIconKatsu]
        .scaled(art_rect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

    version_label = new QLabel(this);
    version_label->setAttribute(Qt::WA_TransparentForMouseEvents);
    QFont font = soa::ui::assets::fonts[soa::ui::assets::Font::EurostileExtraBlack];
    font.setPixelSize(soa::ui::layout::scaled(soa::ui::layout::text::k_version, window_size));
    font.setWeight(QFont::Black);
    font.setLetterSpacing(QFont::PercentageSpacing, 108);
    version_label->setFont(font);
    version_label->setStyleSheet("color: #747B82; background: transparent;");
    version_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    version_label->setGeometry(soa::ui::layout::chrome::version(window_size));
    version_label->raise();
    retranslate_dynamic_text();
}

void MainWindow::setup_settings()
{
    settings = new Settings(shell, this);
    settings->move(0, 0);
    settings->hide();

    connect(settings, &Settings::closed, this, [this]()
    {
        on_overlay_closed(settings);
    });

}

void MainWindow::open_launcher_settings()
{
    open_overlay(settings);
}

void MainWindow::setup_prerequisites()
{
    prerequisites_intro = new PrerequisitesIntro(this);
    prerequisites_intro->hide();

    connect(prerequisites_intro, &PrerequisitesIntro::accepted, this, [this]()
    {
        close_overlay(prerequisites_intro);
        Config::instance().set_prerequisites_confirmed(true);
    });
    connect(prerequisites_intro, &PrerequisitesIntro::choose_own_requested, this, [this]()
    {
        close_overlay(prerequisites_intro);

        auto& config = Config::instance();
        config.set_prerequisites_confirmed(true);
        config.set_runtime_selected(false);

        install_state->probe();
        open_for_current_stage();
    });
}

void MainWindow::setup_rules()
{
    rules_agreement = new RulesAgreement(this);
    rules_agreement->hide();

    connect(rules_agreement, &RulesAgreement::accepted, this, [this]()
    {
        install_state->confirm_rules_reviewed();
        close_overlay(rules_agreement);
    });
}

void MainWindow::setup_repair_files()
{
    repair_files = new RepairFiles(this);
    repair_files->hide();

    connect(repair_files, &RepairFiles::closed, this, [this]()
    {
        on_overlay_closed(repair_files);
    });
    connect(repair_files, &RepairFiles::repair_requested, this, [this]()
    {
        if (shell && shell->is_busy())
        {
            soa::app::detail::show_modeless_message(
                this, LauncherDialog::Tone::Warning, QStringLiteral("Repair Unavailable"),
                QStringLiteral(
                    "Alicia or another runtime operation is active. Finish it before "
                    "changing game files."));
            return;
        }
        close_overlay(repair_files);

        repair_active = true;
        install_state->begin_repair_transfer();
        if (integrity_watcher)
            integrity_watcher->set_suspended(true);
        set_game_switching_enabled(install_state->stage());

        if (!repair_progress)
        {
            repair_progress = new DownloadProgress(DownloadProgress::Mode::Repair, this);
            connect(repair_progress, &DownloadProgress::download_finished, this, [this](const bool ok)
            {
                if (!ok)
                    return;

                repair_active = false;
                install_state->end_repair_transfer();
                pending_integrity_repairs.remove(static_cast<int>(game_version));
                repair_files->set_detected_changes({});
                close_overlay(repair_progress);
                if (integrity_watcher)
                    integrity_watcher->set_suspended(false);
                install_state->mark_game_synchronized();
                last_view = View::Loading;
                on_stage_changed(install_state->stage());
                LauncherDialog::information(
                    this, QStringLiteral("Repair Complete"),
                    QStringLiteral("The selected game was verified and repaired."));
            });
            connect(repair_progress, &DownloadProgress::closed, this, [this]()
            {
                repair_active = false;
                install_state->end_repair_transfer();
                if (integrity_watcher)
                    integrity_watcher->set_suspended(false);
                on_overlay_closed(repair_progress);
                last_view = View::Loading;
                on_stage_changed(install_state->stage());
            });
        }

        open_overlay(repair_progress);
        repair_progress->start_download();
    });
}

void MainWindow::setup_integrity_watcher()
{
    integrity_watcher = new soa::runtime::GameIntegrityWatcher(this);
    connect(integrity_watcher, &soa::runtime::GameIntegrityWatcher::protected_files_changed,
            this, [this](const soa::common::game::GameVersion version, const QStringList& paths)
    {
        if (install_state->stage() == Stage::Downloading
            || install_state->stage() == Stage::Updating
            || repair_active
            || (repair_progress && repair_progress->isVisible()))
        {
            return;
        }

        launch_after_preflight_check = false;
        if (install_state->stage() == Stage::CheckingUpdate)
        {
            install_state->cancel_prelaunch_check();
            if (update_progress)
            {
                update_progress->stop_observing_operation();
                close_overlay(update_progress);
            }
        }

        const int repair_key = static_cast<int>(version);
        const bool already_pending = pending_integrity_repairs.contains(repair_key);
        QStringList detected = pending_integrity_repairs.value(repair_key);
        detected.append(paths);
        detected.removeDuplicates();
        pending_integrity_repairs.insert(repair_key, detected);
        if (already_pending)
        {
            if (version == game_version && repair_files && repair_files->isVisible())
                repair_files->set_detected_changes(detected);
            return;
        }

        close_overlay(settings);
        close_overlay(prerequisites_intro);
        close_overlay(rules_agreement);
        close_overlay(wine_select);
        close_overlay(wine_install);
        close_overlay(game_install);
        set_game_version(version);
        repair_files->set_game_version(version);
        repair_files->set_detected_changes(detected);
        open_overlay(repair_files);
        show_launcher();
    });
    integrity_watcher->refresh();
}

void MainWindow::setup_alicia_chooser()
{
    wine_install = new WineInstall(shell, this);

    const QSize window_size = size();
    alicia_chooser = new AliciaChooser(auth, shell, install_state, this);
    alicia_chooser->set_game_version(game_version);
    alicia_chooser->move(soa::ui::layout::alicia_chooser::pos(window_size));

    connect(alicia_chooser, &AliciaChooser::settings_requested, this, [this]()
    {
        open_overlay(settings);
    });

    connect(alicia_chooser, &AliciaChooser::download_triggered, this, [this]()
    {
        open_for_current_stage();
    });
    connect(alicia_chooser, &AliciaChooser::launch_requested,
            this, &MainWindow::request_game_launch);

    connect(alicia_chooser, &AliciaChooser::reset_config_requested, this, [this]()
    {
        const char* preservedData =
            "The Wine prefix and both game installations will not be deleted.";
        const bool confirmed = LauncherDialog::confirm(
            this,
            LauncherDialog::Tone::Warning,
            QStringLiteral("Reset Launcher Config"),
            soa::i18n::translate(
                "This resets launcher settings, the setup/rules confirmations, and signs you out.\n\n")
                + soa::i18n::translate(preservedData),
            QStringLiteral("Reset Launcher"),
            QStringLiteral("Cancel"),
            true);

        if (!confirmed) return;

        install_state->clear_rules_reviewed();
        Config::instance().reset_launcher_config();
        game_version = Config::instance().game_version();
        alicia_chooser->set_game_version(game_version);
        game_install->refresh_game_path();
        refresh_game_selector();
        install_state->probe();
        update();
    });
}

void MainWindow::setup_game_selector()
{
    const QSize window_size = size();

    playtest_button = soa::ui::simple_utils::make_flat_button(this);
    playtest_button->setCursor(Qt::PointingHandCursor);
    playtest_button->setAccessibleName("Story of Alicia Playtest");
    playtest_button->setGeometry(soa::ui::layout::chrome::playtest_button(window_size));
    connect(playtest_button, &QPushButton::clicked, this, [this]()
    {
        set_game_version(GameVersion::Playtest);
    });

    alicia_2_button = soa::ui::simple_utils::make_flat_button(this);
    alicia_2_button->setCursor(Qt::PointingHandCursor);
    alicia_2_button->setAccessibleName("Story of Alicia 2.0");
    alicia_2_button->setGeometry(soa::ui::layout::chrome::alicia_2_button(window_size));
    connect(alicia_2_button, &QPushButton::clicked, this, [this]()
    {
        set_game_version(GameVersion::Alicia2);
    });

    refresh_game_selector();
}

void MainWindow::setup_wine_install()
{
    wine_install->hide();

    connect(wine_install, &WineInstall::closed, this, [this]()
    {
        on_overlay_closed(wine_install);
    });
}

void MainWindow::setup_game_install()
{
    game_install = new GameInstall(shell, this);
    game_install->hide();

    connect(game_install, &GameInstall::closed, this, [this]()
    {
        on_overlay_closed(game_install);
    });

    update_progress = new DownloadProgress(this);
    update_progress->hide();
    connect(install_state, &soa::ui::InstallState::game_sync_started,
            this, [this](const qulonglong operation_id)
    {
        update_progress->observe_operation(operation_id);
        open_overlay(update_progress);
    });
    connect(install_state, &soa::ui::InstallState::game_sync_finished,
            this, [this](const bool ok)
    {
        update_progress->stop_observing_operation();
        close_overlay(update_progress);
        if (!ok)
            launch_after_preflight_check = false;
    });
    connect(install_state, &soa::ui::InstallState::game_repair_required,
            this, [this](const QStringList& changes)
    {
        launch_after_preflight_check = false;
        const int repair_key = static_cast<int>(game_version);
        QStringList detected = pending_integrity_repairs.value(repair_key);
        detected.append(changes);
        detected.removeDuplicates();
        pending_integrity_repairs.insert(repair_key, detected);
        repair_files->set_game_version(game_version);
        repair_files->set_detected_changes(detected);
        open_overlay(repair_files);
        show_launcher();
    });
    connect(update_progress, &DownloadProgress::external_cancel_requested,
            this, [this](const qulonglong)
    {
        launch_after_preflight_check = false;
        install_state->cancel_prelaunch_check();
        close_overlay(update_progress);
    });
    connect(update_progress, &DownloadProgress::download_finished, this, [this](const bool ok)
    {
        if (!ok)
            return;

        close_overlay(update_progress);
        install_state->mark_game_synchronized();
        last_view = View::Loading;
        on_stage_changed(install_state->stage());
    });
    connect(update_progress, &DownloadProgress::closed, this, [this]()
    {
        on_overlay_closed(update_progress);
    });
}

void MainWindow::setup_wine_select()
{
    wine_select = new WineSelectMenu(this);
    wine_select->hide();

    connect(wine_select, &WineSelectMenu::runtime_chosen, this, [this]()
    {
        close_overlay(wine_select);
        install_state->probe();
        open_for_current_stage();
    });

    connect(wine_select, &WineSelectMenu::closed, this, [this]()
    {
        on_overlay_closed(wine_select);
    });
}

void MainWindow::setup_launcher_updates()
{
    launcher_update_manager = new soa::update::LauncherUpdateManager(this);
    launcher_update = new LauncherUpdate(this);
    launcher_update->hide();

    connect(launcher_update_manager,
            &soa::update::LauncherUpdateManager::check_started,
            this, [this]()
    {
        if (launcher_menu_controller)
            launcher_menu_controller->set_manage_versions_enabled(false);
    });
    connect(launcher_update_manager,
            &soa::update::LauncherUpdateManager::no_update_available,
            this, [this]()
    {
        if (launcher_menu_controller)
            launcher_menu_controller->set_manage_versions_enabled(true);
        continue_after_launcher_update_check();
    });
    connect(launcher_update_manager,
            &soa::update::LauncherUpdateManager::check_failed,
            this, [this](const QString&)
    {
        if (launcher_menu_controller)
            launcher_menu_controller->set_manage_versions_enabled(true);
        continue_after_launcher_update_check();
    });
    connect(launcher_update_manager,
            &soa::update::LauncherUpdateManager::manual_check_failed,
            this, [this](const QString& reason)
    {
        if (launcher_menu_controller)
            launcher_menu_controller->set_manage_versions_enabled(true);
        LauncherDialog::warning(
            this,
            QStringLiteral("Launcher Update Check Failed"),
            reason,
            QStringLiteral("No launcher files were changed."));
    });
    connect(launcher_update_manager,
            &soa::update::LauncherUpdateManager::update_found,
            this, [this]()
    {
        if (launcher_menu_controller)
            launcher_menu_controller->set_manage_versions_enabled(true);
        const QStringList versions = launcher_update_manager->available_versions();
        QString selected_version = launcher_update_manager->available_version();
        if (!versions.isEmpty()
            && launcher_update_manager->select_version(versions.constFirst()))
        {
            selected_version = versions.constFirst();
        }
        launcher_update->set_versions(
            soa::update::LauncherUpdateManager::current_version(),
            versions, false);
        launcher_update->set_release(
            selected_version,
            launcher_update_manager->update_required(),
            launcher_update_manager->release_message());
        open_overlay(launcher_update);
    });
    connect(launcher_update_manager,
            &soa::update::LauncherUpdateManager::catalogue_ready,
            this, [this]()
    {
        if (launcher_menu_controller)
            launcher_menu_controller->set_manage_versions_enabled(true);
        const QStringList versions = launcher_update_manager->available_versions();
        QString selected_version = launcher_update_manager->available_version();
        if (!versions.isEmpty()
            && launcher_update_manager->select_version(versions.constFirst()))
        {
            selected_version = versions.constFirst();
        }
        launcher_update->set_versions(
            soa::update::LauncherUpdateManager::current_version(),
            versions, true);
        launcher_update->set_release(
            selected_version, false,
            launcher_update_manager->release_message());
        open_overlay(launcher_update);
    });
    connect(launcher_update, &LauncherUpdate::postponed, this, [this]()
    {
        launcher_update_manager->cancel_download();
        launcher_update->set_downloading(false);
        close_overlay(launcher_update);
        continue_after_launcher_update_check();
    });
    connect(launcher_update, &LauncherUpdate::update_requested,
            launcher_update_manager,
            &soa::update::LauncherUpdateManager::download_and_install);
    connect(launcher_update, &LauncherUpdate::version_selected,
            launcher_update_manager, &soa::update::LauncherUpdateManager::select_version);
    connect(launcher_update_manager,
            &soa::update::LauncherUpdateManager::download_started,
            this, [this]()
    {
        launcher_update->set_downloading(true);
    });
    connect(launcher_update_manager,
            &soa::update::LauncherUpdateManager::download_progress,
            launcher_update, &LauncherUpdate::set_progress);
    connect(launcher_update_manager,
            &soa::update::LauncherUpdateManager::update_failed,
            this, [this](const QString& reason)
    {
        launcher_update->set_downloading(false);
        LauncherDialog::error(
            this,
            QStringLiteral("Launcher Update Failed"),
            reason,
            QStringLiteral("The existing launcher was not removed. Check the launcher log and try again."));
    });
    connect(launcher_update_manager,
            &soa::update::LauncherUpdateManager::installer_started,
            this, [this](const QString&)
    {
        launcher_update->set_starting_installer();
        QTimer::singleShot(350, this, &MainWindow::request_quit);
    });
}

