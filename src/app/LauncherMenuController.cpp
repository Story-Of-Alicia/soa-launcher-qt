#include "app/LauncherMenuController.hpp"

#include "i18n/LanguageManager.hpp"
#include "ui/Assets.hpp"
#include "ui/Layout.hpp"

#include <QColor>
#include <QAction>
#include <QActionGroup>
#include <QFont>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QWidget>

LauncherMenuController::LauncherMenuController(QWidget* host_, QObject* parent)
    : QObject(parent), host(host_)
{
    if (!host)
        return;

    const QSize window_size = host->size();

    menu_button = new QToolButton(host);
    menu_button->setCheckable(true);
    menu_button->setCursor(Qt::PointingHandCursor);
    menu_button->setGeometry(util::layout::chrome::menu(window_size));
    menu_button->setText(QStringLiteral("☰"));
    menu_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    QFont menu_icon_font = util::assets::fonts[util::assets::Font::EurostileExtraBlack];
    menu_icon_font.setPixelSize(util::layout::scaled(23, window_size));
    menu_icon_font.setWeight(QFont::Black);
    menu_button->setFont(menu_icon_font);
    menu_button->setStyleSheet(QStringLiteral(
        "QToolButton { background: rgba(247,240,235,232); color: #4F1717; "
        "border: 1px solid rgba(79,23,23,72); border-radius: %1px; padding: 0px; }"
        "QToolButton:hover { background: rgba(255,250,247,246); "
        "border-color: rgba(79,23,23,125); }"
        "QToolButton:pressed, QToolButton:checked { background: rgba(232,216,206,246); "
        "border-color: rgba(79,23,23,150); }")
        .arg(util::layout::scaled(6, window_size)));

    menu_panel = new QFrame(host);
    menu_panel->setObjectName(QStringLiteral("launcherMenuPanel"));
    menu_panel->setGeometry(util::layout::scaled(QRect(38, 86, 272, 291), window_size));
    menu_panel->setStyleSheet(QStringLiteral(
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
    auto* menu_shadow = new QGraphicsDropShadowEffect(menu_panel);
    menu_shadow->setBlurRadius(util::layout::scaled(28, window_size));
    menu_shadow->setOffset(0, util::layout::scaled(6, window_size));
    menu_shadow->setColor(QColor(43, 28, 19, 88));
    menu_panel->setGraphicsEffect(menu_shadow);

    QFont button_font = util::assets::fonts[util::assets::Font::EurostileBlack];
    button_font.setPixelSize(util::layout::scaled(15, window_size));
    button_font.setWeight(QFont::Black);

    const int margin = util::layout::scaled(14, window_size);
    const int spacing = util::layout::scaled(10, window_size);
    const int button_height = util::layout::scaled(43, window_size);
    const int button_width = menu_panel->width() - margin * 2;
    const auto make_menu_button = [&](const QString& text, const int row)
    {
        auto* button = new QPushButton(text, menu_panel);
        button->setCursor(Qt::PointingHandCursor);
        button->setFont(button_font);
        button->setGeometry(margin, margin + row * (button_height + spacing),
                            button_width, button_height);
        return button;
    };

    language_button = make_menu_button(QStringLiteral("Language"), 0);
    show_log_button = make_menu_button(QStringLiteral("Show Launcher Log"), 1);
    manage_versions_button = make_menu_button(QStringLiteral("Manage Launcher Versions"), 2);
    credits_button = make_menu_button(QStringLiteral("Credits"), 3);
    about_button = make_menu_button(QStringLiteral("About"), 4);

    language_menu = new QMenu(host);
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
            (void)util::i18n::LanguageManager::instance().set_language(code);
            set_visible(false);
        });
    }

    connect(menu_button, &QToolButton::toggled, this, &LauncherMenuController::set_visible);
    connect(language_button, &QPushButton::clicked, this, [this]()
    {
        const QPoint popup_position = language_button->mapToGlobal(
            QPoint(language_button->width() + util::layout::scaled(8, host->size()), 0));
        language_menu->popup(popup_position);
    });
    connect(show_log_button, &QPushButton::clicked, this, [this]()
    {
        set_visible(false);
        emit show_log_requested();
    });
    connect(manage_versions_button, &QPushButton::clicked, this, [this]()
    {
        set_visible(false);
        manage_versions_button->setEnabled(false);
        emit manage_versions_requested();
    });
    connect(credits_button, &QPushButton::clicked, this, [this]()
    {
        set_visible(false);
        emit credits_requested();
    });
    connect(about_button, &QPushButton::clicked, this, [this]()
    {
        set_visible(false);
        emit about_requested();
    });
    connect(&util::i18n::LanguageManager::instance(),
            &util::i18n::LanguageManager::language_changed,
            this, [this]()
    {
        refresh_language_actions();
        retranslate();
    });

    menu_panel->hide();
    refresh_language_actions();
    retranslate();
}

bool LauncherMenuController::is_visible() const
{
    return menu_panel && menu_panel->isVisible();
}

bool LauncherMenuController::contains(const QPoint& host_position) const
{
    return (menu_panel && menu_panel->geometry().contains(host_position))
        || (menu_button && menu_button->geometry().contains(host_position));
}

void LauncherMenuController::set_visible(const bool visible)
{
    if (!menu_panel || !menu_button)
        return;

    menu_button->blockSignals(true);
    menu_button->setChecked(visible);
    menu_button->blockSignals(false);
    menu_panel->setVisible(visible);
    if (visible)
    {
        menu_panel->raise();
        menu_button->raise();
    }
    else if (language_menu)
    {
        language_menu->close();
    }
}

void LauncherMenuController::raise_controls(const bool chrome_hidden)
{
    if (!menu_panel || !menu_button)
        return;

    if (menu_panel->isVisible())
        menu_panel->raise();
    menu_button->setVisible(!chrome_hidden);
    if (!chrome_hidden)
        menu_button->raise();
    else
        set_visible(false);
}

void LauncherMenuController::set_manage_versions_enabled(const bool enabled)
{
    if (manage_versions_button)
        manage_versions_button->setEnabled(enabled);
}

void LauncherMenuController::retranslate()
{
    if (language_button)
        language_button->setText(util::i18n::translate("Language"));
    if (show_log_button)
        show_log_button->setText(util::i18n::translate("Show Launcher Log"));
    if (manage_versions_button)
        manage_versions_button->setText(util::i18n::translate("Manage Launcher Versions"));
    if (credits_button)
        credits_button->setText(util::i18n::translate("Credits"));
    if (about_button)
        about_button->setText(util::i18n::translate("About"));
    if (menu_button)
    {
        menu_button->setText(QStringLiteral("☰"));
        menu_button->setToolTip(util::i18n::translate("Open launcher menu"));
        menu_button->setAccessibleName(util::i18n::translate("Open launcher menu"));
    }
}

void LauncherMenuController::refresh_language_actions()
{
    if (!language_action_group)
        return;
    const QString current = util::i18n::LanguageManager::instance().current_language();
    for (QAction* action : language_action_group->actions())
        action->setChecked(action->data().toString() == current);
}
