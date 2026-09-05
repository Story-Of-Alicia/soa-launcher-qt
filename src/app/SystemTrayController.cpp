#include "app/SystemTrayController.hpp"

#include "i18n/LanguageManager.hpp"

#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>

SystemTrayController::SystemTrayController(QObject* parent)
    : QObject(parent)
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    tray_menu = new QMenu;
    open_action = tray_menu->addAction(QStringLiteral("Open Launcher"));
    run_action = tray_menu->addAction(QStringLiteral("Run Alicia Directly"));
    tray_menu->addSeparator();
    quit_action = tray_menu->addAction(QStringLiteral("Quit Launcher"));

    connect(open_action, &QAction::triggered, this, &SystemTrayController::open_requested);
    connect(run_action, &QAction::triggered, this, &SystemTrayController::run_requested);
    connect(quit_action, &QAction::triggered, this, &SystemTrayController::quit_requested);
    connect(tray_menu, &QMenu::aboutToShow, this, &SystemTrayController::about_to_show);

    tray_icon = new QSystemTrayIcon(QIcon(QStringLiteral(":/assets/soa-logo.png")), this);
    tray_icon->setToolTip(QStringLiteral("Story Of Alicia Launcher"));
    tray_icon->setContextMenu(tray_menu);
    connect(tray_icon, &QSystemTrayIcon::activated, this,
            [this](const QSystemTrayIcon::ActivationReason reason)
    {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            emit open_requested();
    });
    tray_icon->show();
    retranslate();
}

SystemTrayController::~SystemTrayController()
{
    if (tray_icon)
        tray_icon->setContextMenu(nullptr);
    delete tray_menu;
}

bool SystemTrayController::is_visible() const
{
    return tray_icon && tray_icon->isVisible();
}

void SystemTrayController::set_run_enabled(const bool enabled)
{
    if (run_action)
        run_action->setEnabled(enabled);
}

void SystemTrayController::hide()
{
    if (tray_icon)
        tray_icon->hide();
}

void SystemTrayController::retranslate()
{
    if (open_action)
        open_action->setText(util::i18n::translate("Open Launcher"));
    if (run_action)
        run_action->setText(util::i18n::translate("Run Alicia Directly"));
    if (quit_action)
        quit_action->setText(util::i18n::translate("Quit Launcher"));
}
