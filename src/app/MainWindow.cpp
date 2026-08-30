#include "app/MainWindow.hpp"

#include "auth/AuthHandler.hpp"
#include "network/DiscordRpc.hpp"
#include "runtime/GameIntegrityWatcher.hpp"
#include "update/LauncherUpdateManager.hpp"
#include "ui/InstallState.hpp"
#include "ui/ViewRouter.hpp"
#include "runtime/Shell.hpp"
#include "ui/Assets.hpp"
#include "config/Config.hpp"
#include "ui/Layout.hpp"
#include "i18n/LanguageManager.hpp"
#include "ui/ModalOverlay.hpp"
#include "ui/SimpleUtils.hpp"
#include "ui/AliciaChooser.hpp"
#include "ui/PrerequisitesIntro.hpp"
#include "ui/RulesAgreement.hpp"
#include "ui/RepairFiles.hpp"
#include "ui/GameInstall.hpp"
#include "ui/DownloadProgress.hpp"
#include "ui/Settings.hpp"
#include "ui/WineInstall.hpp"
#include "ui/WineSelectMenu.hpp"
#include "ui/LauncherLog.hpp"
#include "ui/LauncherInfoDialog.hpp"
#include "ui/LauncherDialog.hpp"
#include "ui/LauncherUpdate.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QGraphicsDropShadowEffect>
#include <QFrame>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QSystemTrayIcon>
#include <QToolButton>
#include <QTimer>
#include <QWindow>

using core::game::GameVersion;
using core::state::Stage;
using core::state::View;
using util::config::Config;

#ifndef SOA_LAUNCHER_VERSION
#define SOA_LAUNCHER_VERSION "0.3.0"
#endif

namespace
{
    LauncherDialog* show_modeless_message(QWidget* parent,
                                           const LauncherDialog::Tone tone,
                                           const QString& title,
                                           const QString& message,
                                           const QString& informative = {})
    {
        return LauncherDialog::open_message(parent, tone, title, message, informative);
    }

    QSize configured_window_size()
    {
        const QString configured = Config::instance().launcher_size();
        const QStringList parts = configured.split(QLatin1Char('x'));
        QSize requested = util::layout::win::k_default;
        if (parts.size() == 2)
        {
            bool width_ok = false;
            bool height_ok = false;
            const int width = parts[0].toInt(&width_ok);
            const int height = parts[1].toInt(&height_ok);
            if (width_ok && height_ok && width >= 640 && height >= 360)
                requested = QSize(width, height);
        }

        QScreen* screen = QGuiApplication::primaryScreen();
        if (!screen)
            return requested;
        const QSize available = screen->availableGeometry().size() - QSize(24, 24);
        const double scale = qMin(1.0, qMin(
            static_cast<double>(available.width()) / requested.width(),
            static_cast<double>(available.height()) / requested.height()));
        return QSize(qMax(1, qMin(available.width(), qRound(requested.width() * scale))),
                     qMax(1, qMin(available.height(), qRound(requested.height() * scale))));
    }
    QPixmap make_version_button(const QSize window_size, const QRect button_rect,
                                const QPixmap& frame, const QPixmap& icon,
                                const QPoint icon_offset)
    {
        QPixmap composed(button_rect.size());
        composed.fill(Qt::transparent);

        QPainter painter(&composed);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        const QSize icon_size = util::layout::scaled(icon.size(), window_size);
        painter.drawPixmap(icon_offset, icon.scaled(icon_size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        painter.drawPixmap(QRect(QPoint(0, 0), button_rect.size()), frame);

        return composed;
    }
}

MainWindow::MainWindow(QWidget* parent)
    : QWidget(parent),
      game_version(Config::instance().game_version())
{
    setWindowFlags(Qt::FramelessWindowHint);
    setFixedSize(configured_window_size());
    setAttribute(Qt::WA_TranslucentBackground);

    shell = new core::wine::Shell(this);
    auth = new AuthHandler(shell, this);
    install_state = new core::state::InstallState(this);

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
        LauncherDialog* box = show_modeless_message(
            this, LauncherDialog::Tone::Error, QStringLiteral("Launcher Error"), message,
            QStringLiteral(
                "The launcher log has diagnostic details. Close this message, then retry the action."));
        connect(box, &QDialog::finished, install_state,
                [this]() { install_state->dismiss_error(); });
    });
    connect(shell, &core::wine::Shell::user_notice, this, [this](const QString& message)
    {
        show_modeless_message(this, LauncherDialog::Tone::Information,
                               QStringLiteral("Story of Alicia Launcher"), message);
    });
    connect(&Config::instance(), &Config::persistence_failed, this,
            [this](const QString& path, const QString& reason)
    {
        show_modeless_message(
            this, LauncherDialog::Tone::Error, QStringLiteral("Settings Not Saved"),
            util::i18n::translate(
                "The launcher could not save config.json.\n\nPath: %1\nReason: %2\n\n"
                "Your on-screen change is active only for this session.")
                .arg(path, reason));
    });
    if (!Config::instance().persistence_error().isEmpty())
    {
        show_modeless_message(
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
        refresh_language_actions();
        retranslate_dynamic_text();
        raise_persistent_controls();
    });
}

MainWindow::~MainWindow()
{
    if (tray_icon)
        tray_icon->hide();
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
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    tray_menu = new QMenu(this);
    open_launcher_action = tray_menu->addAction(QStringLiteral("Open Launcher"));
    run_alicia_action = tray_menu->addAction(QStringLiteral("Run Alicia Directly"));
    tray_menu->addSeparator();
    tray_quit_action = tray_menu->addAction(QStringLiteral("Quit Launcher"));

    connect(open_launcher_action, &QAction::triggered, this, &MainWindow::show_launcher);
    connect(run_alicia_action, &QAction::triggered, this, &MainWindow::run_game_directly);
    connect(tray_quit_action, &QAction::triggered, this, &MainWindow::request_quit);
    connect(tray_menu, &QMenu::aboutToShow, this, &MainWindow::refresh_tray_actions);

    tray_icon = new QSystemTrayIcon(QIcon(QStringLiteral(":/assets/soa-logo.png")), this);
    tray_icon->setToolTip(QStringLiteral("Story Of Alicia Launcher"));
    tray_icon->setContextMenu(tray_menu);
    connect(tray_icon, &QSystemTrayIcon::activated, this,
            [this](const QSystemTrayIcon::ActivationReason reason)
    {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            show_launcher();
    });
    tray_icon->show();
}

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
    if (!run_alicia_action)
        return;

    run_alicia_action->setEnabled(can_run_game_directly());
}

void MainWindow::setup_window_buttons()
{
    const QSize window_size = size();

    close_button = util::simple_utils::make_flat_button(this);
    close_button->setIcon(QIcon(util::assets::images[util::assets::Image::CloseIcon]));
    close_button->setIconSize(util::layout::chrome::close_icon(window_size));
    close_button->setGeometry(util::layout::chrome::close(window_size));
    close_button->setAccessibleName(QStringLiteral("Close launcher"));
    connect(close_button, &QPushButton::clicked, this, [this]()
    {
        close();
    });

    minimize_button = util::simple_utils::make_flat_button(this);
    minimize_button->setIcon(QIcon(util::assets::images[util::assets::Image::Minimize]));
    minimize_button->setIconSize(util::layout::chrome::minimize_icon(window_size));
    minimize_button->setGeometry(util::layout::chrome::minimize(window_size));
    minimize_button->setAccessibleName(QStringLiteral("Minimize launcher"));
    connect(minimize_button, &QPushButton::clicked, this, [this]()
    {
        showMinimized();
    });
}


void MainWindow::setup_launcher_menu()
{
    const QSize window_size = size();

    launcher_menu_button = new QToolButton(this);
    launcher_menu_button->setCheckable(true);
    launcher_menu_button->setCursor(Qt::PointingHandCursor);
    launcher_menu_button->setGeometry(util::layout::chrome::menu(window_size));
    launcher_menu_button->setText(QStringLiteral("☰"));
    launcher_menu_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    launcher_menu_button->setAccessibleName(QStringLiteral("Open launcher menu"));
    QFont menu_icon_font = util::assets::fonts[util::assets::Font::EurostileExtraBlack];
    menu_icon_font.setPixelSize(util::layout::scaled(23, window_size));
    menu_icon_font.setWeight(QFont::Black);
    launcher_menu_button->setFont(menu_icon_font);
    launcher_menu_button->setStyleSheet(QStringLiteral(
        "QToolButton { background: rgba(247,240,235,232); color: #4F1717; "
        "border: 1px solid rgba(79,23,23,72); border-radius: %1px; padding: 0px; }"
        "QToolButton:hover { background: rgba(255,250,247,246); "
        "border-color: rgba(79,23,23,125); }"
        "QToolButton:pressed, QToolButton:checked { background: rgba(232,216,206,246); "
        "border-color: rgba(79,23,23,150); }")
        .arg(util::layout::scaled(6, window_size)));

    launcher_menu_panel = new QFrame(this);
    launcher_menu_panel->setObjectName(QStringLiteral("launcherMenuPanel"));
    launcher_menu_panel->setGeometry(
        util::layout::scaled(QRect(38, 86, 272, 291), window_size));
    launcher_menu_panel->setStyleSheet(QStringLiteral(
        "QFrame#launcherMenuPanel { background: rgba(247,240,235,248); "
        "border: 1px solid rgba(79,23,23,85); border-radius: 0px; }"
        "QPushButton { background: rgba(255,255,255,132); color: #4F1717; "
        "border: 1px solid rgba(79,23,23,38); border-radius: %1px; "
        "padding-left: %2px; text-align: left; }"
        "QPushButton:hover { background: rgba(235,220,211,224); "
        "border-color: rgba(79,23,23,92); }"
        "QPushButton:pressed { background: rgba(219,198,186,236); "
        "border-color: rgba(79,23,23,125); }")
        .arg(util::layout::scaled(6, window_size))
        .arg(util::layout::scaled(17, window_size)));
    auto* menu_shadow = new QGraphicsDropShadowEffect(launcher_menu_panel);
    menu_shadow->setBlurRadius(util::layout::scaled(28, window_size));
    menu_shadow->setOffset(0, util::layout::scaled(6, window_size));
    menu_shadow->setColor(QColor(43, 28, 19, 88));
    launcher_menu_panel->setGraphicsEffect(menu_shadow);

    QFont button_font = util::assets::fonts[util::assets::Font::EurostileBlack];
    button_font.setPixelSize(util::layout::scaled(15, window_size));
    button_font.setWeight(QFont::Black);

    const int margin = util::layout::scaled(14, window_size);
    const int spacing = util::layout::scaled(10, window_size);
    const int button_height = util::layout::scaled(43, window_size);
    const int button_width = launcher_menu_panel->width() - margin * 2;
    const auto make_menu_button = [&](const QString& text, const int row)
    {
        auto* button = new QPushButton(text, launcher_menu_panel);
        button->setCursor(Qt::PointingHandCursor);
        button->setFont(button_font);
        button->setGeometry(margin, margin + row * (button_height + spacing),
                            button_width, button_height);
        return button;
    };

    language_button = make_menu_button(QStringLiteral("Language"), 0);
    show_log_button = make_menu_button(QStringLiteral("Show Launcher Log"), 1);
    check_updates_button = make_menu_button(QStringLiteral("Manage Launcher Versions"), 2);
    credits_button = make_menu_button(QStringLiteral("Credits"), 3);
    about_button = make_menu_button(QStringLiteral("About"), 4);

    language_menu = new QMenu(this);
    language_menu->setStyleSheet(QStringLiteral(
        "QMenu { background: #F7F0EB; color: #4F1717; border: 1px solid #A98678; "
        "border-radius: 0px; padding: %1px; font-size: %2px; }"
        "QMenu::item { min-width: %3px; padding: %4px %5px %4px %6px; "
        "border-radius: %7px; }"
        "QMenu::item:selected { background: #EBDCD3; }"
        "QMenu::indicator { width: %8px; height: %8px; }")
        .arg(util::layout::scaled(6, window_size))
        .arg(qMax(9, util::layout::scaled(13, window_size)))
        .arg(util::layout::scaled(170, window_size))
        .arg(util::layout::scaled(9, window_size))
        .arg(util::layout::scaled(28, window_size))
        .arg(util::layout::scaled(12, window_size))
        .arg(util::layout::scaled(6, window_size))
        .arg(util::layout::scaled(14, window_size)));
    language_action_group = new QActionGroup(this);
    language_action_group->setExclusive(true);
    for (const auto& language : util::i18n::LanguageManager::instance().languages())
    {
        QAction* action = language_menu->addAction(language.native_name);
        action->setCheckable(true);
        action->setData(language.code);
        language_action_group->addAction(action);
        connect(action, &QAction::triggered, this, [this, code = language.code]()
        {
            util::i18n::LanguageManager::instance().set_language(code);
            set_launcher_menu_visible(false);
        });
    }

    connect(launcher_menu_button, &QToolButton::toggled,
            this, &MainWindow::set_launcher_menu_visible);
    connect(language_button, &QPushButton::clicked, this, [this]()
    {
        const QPoint popup_position = language_button->mapToGlobal(
            QPoint(language_button->width() + util::layout::scaled(8, size()), 0));
        language_menu->popup(popup_position);
    });
    connect(show_log_button, &QPushButton::clicked, this, [this]()
    {
        set_launcher_menu_visible(false);
        LauncherLog* log = LauncherLog::instance();
        log->showNormal();
        log->raise();
        log->activateWindow();
    });
    connect(check_updates_button, &QPushButton::clicked, this, [this]()
    {
        set_launcher_menu_visible(false);
        if (!launcher_update_manager)
            return;
        check_updates_button->setEnabled(false);
        launcher_update_manager->manage_versions();
    });
    connect(about_button, &QPushButton::clicked, this, [this]()
    {
        set_launcher_menu_visible(false);
        show_about();
    });
    connect(credits_button, &QPushButton::clicked, this, [this]()
    {
        set_launcher_menu_visible(false);
        show_credits();
    });

    launcher_menu_panel->hide();
    refresh_language_actions();
    retranslate_dynamic_text();
}

void MainWindow::refresh_language_actions()
{
    if (!language_action_group)
        return;
    const QString current = util::i18n::LanguageManager::instance().current_language();
    for (QAction* action : language_action_group->actions())
        action->setChecked(action->data().toString() == current);
}

void MainWindow::set_launcher_menu_visible(const bool visible)
{
    if (!launcher_menu_panel || !launcher_menu_button)
        return;

    launcher_menu_button->blockSignals(true);
    launcher_menu_button->setChecked(visible);
    launcher_menu_button->blockSignals(false);
    launcher_menu_panel->setVisible(visible);
    if (visible)
    {
        launcher_menu_panel->raise();
        launcher_menu_button->raise();
    }
    else if (language_menu)
    {
        language_menu->close();
    }
}

void MainWindow::raise_persistent_controls()
{
    if (launcher_menu_panel && launcher_menu_panel->isVisible())
        launcher_menu_panel->raise();
    if (launcher_menu_button)
    {
        launcher_menu_button->setVisible(!chrome_hidden);
        if (!chrome_hidden)
            launcher_menu_button->raise();
    }
    if (chrome_hidden && launcher_menu_panel)
        launcher_menu_panel->hide();
    if (!chrome_hidden)
    {



        if (close_button) close_button->raise();
        if (minimize_button) minimize_button->raise();
    }
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

void MainWindow::request_quit()
{
    force_quit_requested = true;
    set_launcher_menu_visible(false);
    if (tray_icon)
        tray_icon->hide();
    if (language_menu)
        language_menu->close();
    close();
    QTimer::singleShot(0, qApp, []()
    {
        QCoreApplication::exit(0);
    });
}

void MainWindow::retranslate_dynamic_text()
{
    if (language_button)
        language_button->setText(util::i18n::translate("Language"));
    if (show_log_button)
        show_log_button->setText(util::i18n::translate("Show Launcher Log"));
    if (check_updates_button)
        check_updates_button->setText(util::i18n::translate("Manage Launcher Versions"));
    if (about_button)
        about_button->setText(util::i18n::translate("About"));
    if (credits_button)
        credits_button->setText(util::i18n::translate("Credits"));
    if (open_launcher_action)
        open_launcher_action->setText(util::i18n::translate("Open Launcher"));
    if (run_alicia_action)
        run_alicia_action->setText(util::i18n::translate("Run Alicia Directly"));
    if (tray_quit_action)
        tray_quit_action->setText(util::i18n::translate("Quit Launcher"));
    if (launcher_menu_button)
    {
        launcher_menu_button->setText(QStringLiteral("☰"));
        launcher_menu_button->setToolTip(util::i18n::translate("Open launcher menu"));
        launcher_menu_button->setAccessibleName(util::i18n::translate("Open launcher menu"));
    }
    if (close_button)
        close_button->setAccessibleName(util::i18n::translate("Close launcher"));
    if (minimize_button)
        minimize_button->setAccessibleName(util::i18n::translate("Minimize launcher"));
    if (version_label)
        version_label->setText(util::i18n::translate("VERSION") + QLatin1Char(' ')
            + QString::fromLatin1(SOA_LAUNCHER_VERSION));
}

void MainWindow::setup_version_label()
{
    const QSize window_size = size();

    version_art_label = new QLabel(this);
    version_art_label->setAttribute(Qt::WA_TransparentForMouseEvents);
    version_art_label->setAlignment(Qt::AlignRight | Qt::AlignBottom);
    const QRect art_rect = util::layout::chrome::version_art(window_size);
    version_art_label->setGeometry(art_rect);
    version_art_label->setPixmap(util::assets::images[util::assets::Image::VersionIconKatsu]
        .scaled(art_rect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

    version_label = new QLabel(this);
    version_label->setAttribute(Qt::WA_TransparentForMouseEvents);
    QFont font = util::assets::fonts[util::assets::Font::EurostileExtraBlack];
    font.setPixelSize(util::layout::scaled(util::layout::text::k_version, window_size));
    font.setWeight(QFont::Black);
    font.setLetterSpacing(QFont::PercentageSpacing, 108);
    version_label->setFont(font);
    version_label->setStyleSheet("color: #747B82; background: transparent;");
    version_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    version_label->setGeometry(util::layout::chrome::version(window_size));
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
            show_modeless_message(
                this, LauncherDialog::Tone::Warning, QStringLiteral("Repair Unavailable"),
                QStringLiteral(
                    "Alicia or another runtime operation is active. Finish it before "
                    "changing game files."));
            return;
        }
        close_overlay(repair_files);

        repair_active = true;
        repair_files->set_detected_changes({});
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
                close_overlay(repair_progress);
                if (integrity_watcher)
                    integrity_watcher->set_suspended(false);
                install_state->probe();
                last_view = View::Loading;
                on_stage_changed(install_state->stage());
                LauncherDialog::information(
                    this, QStringLiteral("Repair Complete"),
                    QStringLiteral("The selected game was verified and repaired."));
            });
            connect(repair_progress, &DownloadProgress::closed, this, [this]()
            {
                repair_active = false;
                if (integrity_watcher)
                    integrity_watcher->set_suspended(false);
                on_overlay_closed(repair_progress);
                last_view = View::Loading;
                on_stage_changed(install_state->stage());
            });
        }

        open_overlay(repair_progress);
    });
}


void MainWindow::setup_integrity_watcher()
{
    integrity_watcher = new core::integrity::GameIntegrityWatcher(this);
    connect(integrity_watcher, &core::integrity::GameIntegrityWatcher::protected_files_changed,
            this, [this](const core::game::GameVersion version, const QStringList& paths)
    {
        if (repair_active || (repair_files && repair_files->isVisible())
            || (repair_progress && repair_progress->isVisible()))
        {
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
        repair_files->set_detected_changes(paths);
        open_overlay(repair_files);
        showNormal();
        raise();
        activateWindow();
    });
    integrity_watcher->refresh();
}

void MainWindow::setup_alicia_chooser()
{
    wine_install = new WineInstall(shell, this);

    const QSize window_size = size();
    alicia_chooser = new AliciaChooser(auth, shell, install_state, this);
    alicia_chooser->set_game_version(game_version);
    alicia_chooser->move(util::layout::alicia_chooser::pos(window_size));

    connect(alicia_chooser, &AliciaChooser::settings_requested, this, [this]()
    {
        open_overlay(settings);
    });

    connect(alicia_chooser, &AliciaChooser::download_triggered, this, [this]()
    {
        open_for_current_stage();
    });

    connect(alicia_chooser, &AliciaChooser::reset_config_requested, this, [this]()
    {
        const char* preservedData =
            "The Wine prefix and both game installations will not be deleted.";
        const bool confirmed = LauncherDialog::confirm(
            this,
            LauncherDialog::Tone::Warning,
            QStringLiteral("Reset Launcher Config"),
            util::i18n::translate(
                "This resets launcher settings, the setup/rules confirmations, and signs you out.\n\n")
                + util::i18n::translate(preservedData),
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

    playtest_button = util::simple_utils::make_flat_button(this);
    playtest_button->setCursor(Qt::PointingHandCursor);
    playtest_button->setAccessibleName("Story of Alicia Playtest");
    playtest_button->setGeometry(util::layout::chrome::playtest_button(window_size));
    connect(playtest_button, &QPushButton::clicked, this, [this]()
    {
        set_game_version(GameVersion::Playtest);
    });

    alicia_2_button = util::simple_utils::make_flat_button(this);
    alicia_2_button->setCursor(Qt::PointingHandCursor);
    alicia_2_button->setAccessibleName("Story of Alicia 2.0");
    alicia_2_button->setGeometry(util::layout::chrome::alicia_2_button(window_size));
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
    launcher_update_manager = new core::update::LauncherUpdateManager(this);
    launcher_update = new LauncherUpdate(this);
    launcher_update->hide();

    connect(launcher_update_manager,
            &core::update::LauncherUpdateManager::check_started,
            this, [this]()
    {
        if (check_updates_button)
            check_updates_button->setEnabled(false);
    });
    connect(launcher_update_manager,
            &core::update::LauncherUpdateManager::no_update_available,
            this, [this]()
    {
        if (check_updates_button)
            check_updates_button->setEnabled(true);
        continue_after_launcher_update_check();
    });
    connect(launcher_update_manager,
            &core::update::LauncherUpdateManager::check_failed,
            this, [this](const QString&)
    {
        if (check_updates_button)
            check_updates_button->setEnabled(true);
        continue_after_launcher_update_check();
    });
    connect(launcher_update_manager,
            &core::update::LauncherUpdateManager::manual_check_failed,
            this, [this](const QString& reason)
    {
        if (check_updates_button)
            check_updates_button->setEnabled(true);
        LauncherDialog::warning(
            this,
            QStringLiteral("Launcher Update Check Failed"),
            reason,
            QStringLiteral("No launcher files were changed."));
    });
    connect(launcher_update_manager,
            &core::update::LauncherUpdateManager::update_found,
            this, [this]()
    {
        if (check_updates_button)
            check_updates_button->setEnabled(true);
        launcher_update->set_versions(
            core::update::LauncherUpdateManager::current_version(),
            launcher_update_manager->available_versions(), false);
        launcher_update->set_release(
            launcher_update_manager->available_version(),
            launcher_update_manager->update_required(),
            launcher_update_manager->release_message());
        open_overlay(launcher_update);
    });
    connect(launcher_update_manager,
            &core::update::LauncherUpdateManager::catalogue_ready,
            this, [this]()
    {
        if (check_updates_button)
            check_updates_button->setEnabled(true);
        launcher_update->set_versions(
            core::update::LauncherUpdateManager::current_version(),
            launcher_update_manager->available_versions(), true);
        launcher_update->set_release(
            launcher_update_manager->available_version(), false,
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
            &core::update::LauncherUpdateManager::download_and_install);
    connect(launcher_update, &LauncherUpdate::version_selected,
            launcher_update_manager, &core::update::LauncherUpdateManager::select_version);
    connect(launcher_update_manager,
            &core::update::LauncherUpdateManager::download_started,
            this, [this]()
    {
        launcher_update->set_downloading(true);
    });
    connect(launcher_update_manager,
            &core::update::LauncherUpdateManager::download_progress,
            launcher_update, &LauncherUpdate::set_progress);
    connect(launcher_update_manager,
            &core::update::LauncherUpdateManager::update_failed,
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
            &core::update::LauncherUpdateManager::installer_started,
            this, [this](const QString&)
    {
        launcher_update->set_starting_installer();
        QTimer::singleShot(350, this, &MainWindow::request_quit);
    });
}

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

    const QPixmap playtest_pixmap = make_version_button(
        window_size,
        playtest_rect,
        game_version == GameVersion::Playtest ? active : inactive,
        playtest_icon,
        util::layout::chrome::playtest_icon_offset(window_size));

    const QPixmap alicia_2_pixmap = make_version_button(
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
    if (launcher_menu_panel && launcher_menu_panel->isVisible()
        && !launcher_menu_panel->geometry().contains(event->position().toPoint())
        && (!launcher_menu_button
            || !launcher_menu_button->geometry().contains(event->position().toPoint())))
    {
        set_launcher_menu_visible(false);
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

    if (tray_icon && tray_icon->isVisible())
    {
        set_launcher_menu_visible(false);
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
