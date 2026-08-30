#include <QAbstractButton>
#include <QLineEdit>
#include <QPainter>
#include <QVariant>
#include <QPainterPath>
#include <QPushButton>
#include <QStackedWidget>
#include "ui/Settings.hpp"
#include "i18n/LanguageManager.hpp"

#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsBlurEffect>

#include "ui/LauncherSettings.hpp"
#include "ui/ImageDropdown.hpp"
#include "ui/WineSettings.hpp"
#include "ui/AdvancedSettings.hpp"
#include "ui/Layout.hpp"
#include "ui/SimpleUtils.hpp"
#include "ui/Colors.hpp"
#include "spdlog/spdlog.h"
#include "runtime/Shell.hpp"

namespace
{
    constexpr auto k_allow_while_locked = "soa_allow_while_mutation_locked";
    constexpr auto k_previous_enabled = "soa_mutation_previous_enabled";
    constexpr auto k_previous_tooltip = "soa_mutation_previous_tooltip";

    bool allowed_while_locked(const QWidget* widget)
    {
        const QObject* current = widget;
        while (current)
        {
            if (current->property(k_allow_while_locked).toBool())
                return true;
            current = current->parent();
        }
        return false;
    }

    bool is_mutation_control(QWidget* widget)
    {
        return qobject_cast<QAbstractButton*>(widget)
            || qobject_cast<QLineEdit*>(widget)
            || qobject_cast<ImageDropdown*>(widget);
    }

    void set_mutation_control_enabled(QWidget* widget, const bool enabled,
                                      const QString& disabled_reason)
    {
        if (enabled)
        {
            const QVariant previous_enabled = widget->property(k_previous_enabled);
            if (previous_enabled.isValid())
            {
                widget->setEnabled(previous_enabled.toBool());
                widget->setProperty(k_previous_enabled, QVariant{});
            }
            const QVariant previous_tooltip = widget->property(k_previous_tooltip);
            if (previous_tooltip.isValid())
            {
                widget->setToolTip(previous_tooltip.toString());
                widget->setProperty(k_previous_tooltip, QVariant{});
            }
            return;
        }

        if (!widget->property(k_previous_enabled).isValid())
            widget->setProperty(k_previous_enabled, widget->isEnabled());
        if (!widget->property(k_previous_tooltip).isValid())
            widget->setProperty(k_previous_tooltip, widget->toolTip());
        widget->setToolTip(disabled_reason);
        widget->setEnabled(false);
    }
}

Settings::Settings(core::wine::Shell * shell, QWidget* parent) : ModalOverlay(parent), shell(shell)
{
    set_keeps_chrome(false);
    setup_pages();
    setup_close_button();
    setup_tabs();
    connect(&util::i18n::LanguageManager::instance(),
            &util::i18n::LanguageManager::language_changed, this, [this]()
    {
        set_mutation_enabled(mutation_enabled, mutation_reason);
    });
}

void Settings::set_mutation_enabled(const bool enabled, const QString& reason)
{
    mutation_enabled = enabled;
    mutation_reason = reason;
    const QString translated_reason = util::i18n::translate(reason);

    if (stack)
    {
        const auto controls = stack->findChildren<QWidget*>();
        for (QWidget* widget : controls)
        {
            if (!widget || !is_mutation_control(widget) || allowed_while_locked(widget))
                continue;
            set_mutation_control_enabled(widget, enabled, translated_reason);
        }

        stack->setToolTip(QString());
        stack->setAccessibleDescription(enabled
            ? util::i18n::translate("Settings are editable")
            : translated_reason);
    }

    for (QPushButton* button : tab_buttons)
    {
        if (!button)
            continue;
        button->setEnabled(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(QString());
    }
    update();
}

void Settings::setup_pages()
{
    const QSize w = size();
    stack = new QStackedWidget(this);
    stack->setGeometry(util::layout::settings::box_rect(w));
    stack->setStyleSheet(QStringLiteral("QStackedWidget { background: transparent; }"));

    launcher_settings = new LauncherSettings(stack);
    stack->addWidget(launcher_settings);
    connect(launcher_settings, &LauncherSettings::connectivity_panel_changed,
            this, [this](const bool expanded)
    {
        launcher_panel_expanded = expanded;
        update_panel_geometry();
    });
    stack->addWidget(new WineSettings(shell, stack));
    stack->addWidget(new AdvancedSettings(stack));
    stack->setCurrentIndex(0);
}

void Settings::setup_close_button()
{
    const QSize w = size();

    close_button = new QPushButton(this);
    close_button->setFlat(true);
    close_button->setCursor(Qt::PointingHandCursor);
    close_button->setText("");
    close_button->setStyleSheet("border: none; background: transparent;");

    const auto & close_px = util::assets::images[util::assets::Image::CloseSettings];
    close_button->setIcon(QIcon(close_px));
    close_button->setIconSize(util::layout::settings::close_icon(w));
    close_button->setGeometry(util::layout::settings::close(w));
    close_button->setAccessibleName(QStringLiteral("Close settings"));
    close_button->raise();

    connect(close_button, &QPushButton::clicked, this, [this]() { hide(); emit closed(); });
}

void Settings::setup_tabs()
{
    const QSize w = size();

    auto make_tab = [&](const int i)
    {
        auto* b = util::simple_utils::make_flat_button(this);
        b->setGeometry(util::layout::settings::tab_rect(w, i));
        tab_buttons[i] = b;
        return b;
    };

    auto* tab_general = make_tab(0);
    auto* tab_wine = make_tab(1);
    auto* tab_advanced = make_tab(2);
    tab_general->setAccessibleName(QStringLiteral("Launcher settings tab"));
    tab_wine->setAccessibleName(QStringLiteral("Wine settings tab"));
    tab_advanced->setAccessibleName(QStringLiteral("Advanced settings tab"));

    connect(tab_general,  &QPushButton::clicked, this, [this]() { set_tab(0); });
    connect(tab_wine,     &QPushButton::clicked, this, [this]() { set_tab(1); });
    connect(tab_advanced, &QPushButton::clicked, this, [this]() { set_tab(2); });

    tab_general->raise();
    tab_wine->raise();
    tab_advanced->raise();
    close_button->raise();
}

void Settings::set_tab(const int index)
{
    active_tab = index;
    stack->setCurrentIndex(index);
    update_panel_geometry();
}

void Settings::update_panel_geometry()
{
    const QSize w = size();
    const bool expanded = active_tab == 0 && launcher_panel_expanded;
    stack->setGeometry(util::layout::settings::box_rect(w, expanded));
    close_button->setGeometry(util::layout::settings::close(w, expanded));
    for (int i = 0; i < 3; ++i)
    {
        if (tab_buttons[i])
            tab_buttons[i]->setGeometry(util::layout::settings::tab_rect(w, i, expanded));
    }
    close_button->raise();
    update();
}

void Settings::paint_content(QPainter& painter)
{
    const QSize w = size();
    const bool expanded = active_tab == 0 && launcher_panel_expanded;
    const QRect box = util::layout::settings::box_rect(w, expanded);
    painter.drawPixmap(box, util::assets::images[util::assets::Image::BoxSettings]);

    {
        QFont tf = util::assets::fonts[util::assets::Font::EurostileExtraBlack];
        tf.setPixelSize(util::layout::scaled(util::layout::text::k_modal_header, w));
        tf.setWeight(QFont::Black);
        painter.setFont(tf);
        painter.setPen(util::colors::k_text_maroon);
        const char* runtimeTitle = "WINE SETTINGS";
        const char* title =
            (active_tab == 1) ? runtimeTitle :
            (active_tab == 2) ? "ADVANCED SETTINGS" : "LAUNCHER SETTINGS";
        painter.drawText(util::layout::settings::page_title(w, expanded), Qt::AlignCenter,
                         util::i18n::translate(title));
    }

    constexpr QColor active   {0xFB, 0xF6, 0xF0};
    constexpr QColor inactive {0xD8, 0xCD, 0xC0};
    constexpr QColor textCol  {0x4F, 0x17, 0x17};

    QFont f = util::assets::fonts[util::assets::Font::EurostileBlack];
    f.setPixelSize(util::layout::scaled(util::layout::text::k_label, w));
    f.setWeight(QFont::Black);
    painter.setFont(f);
    const int radius = util::layout::settings::tab_radius(w);

    for (int i = 0; i < 3; ++i)
    {
        const char* runtimeLabel = "WINE";
        const char* labels[] = {"LAUNCHER", runtimeLabel, "ADVANCED"};
        const QRect r = util::layout::settings::tab_rect(w, i, expanded);
        const bool  on = i == active_tab;



        const QRectF rf(r);
        const qreal corner =
            qMin<qreal>(radius, qMin(rf.width(), rf.height()) / 2.0);
        QPainterPath p;
        p.moveTo(rf.bottomLeft());
        p.lineTo(rf.left(), rf.top() + corner);
        p.quadTo(rf.left(), rf.top(), rf.left() + corner, rf.top());
        p.lineTo(rf.right() - corner, rf.top());
        p.quadTo(rf.right(), rf.top(), rf.right(), rf.top() + corner);
        p.lineTo(rf.bottomRight());
        p.closeSubpath();
        painter.fillPath(p, on ? active : inactive);
        painter.setPen(on ? textCol : textCol.lighter(140));
        painter.drawText(r, Qt::AlignCenter, util::i18n::translate(labels[i]));
    }
}
