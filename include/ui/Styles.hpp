#pragma once

#include "ui/Layout.hpp"

#include <QSize>
#include <QString>
#include <QtGlobal>

namespace util::styles
{
    inline QString field(const QSize win)
    {
        return QStringLiteral(
            "QLineEdit"
            "{"
            "    background: rgba(255,255,255,0.45);"
            "    border: 1px solid #C9BBAA;"
            "    border-radius: %1px;"
            "    padding: 0 %2px;"
            "    color: #4F1717;"
            "    font-family: 'Inter';"
            "    font-size: %3px;"
            "}"
            "QLineEdit:disabled"
            "{"
            "    background: rgba(231,224,216,0.60);"
            "    border-color: #D8CDC0;"
            "    color: #9E8E7E;"
            "}")
            .arg(layout::scaled(6, win))
            .arg(layout::scaled(8, win))
            .arg(qMax(9, layout::scaled(layout::text::k_body, win)));
    }

    inline QString neutral_button(const QSize win)
    {
        return QStringLiteral(
            "QPushButton"
            "{"
            "    background: #D8CDC0;"
            "    border: none;"
            "    border-radius: %1px;"
            "    color: #4F1717;"
            "    font-family: 'Eurostile';"
            "    font-weight: 900;"
            "    font-size: %2px;"
            "}"
            "QPushButton:hover { background: #E6DCD0; }"
            "QPushButton:pressed { background: #C9BBAA; }"
            "QPushButton:disabled { background: #E7E0D8; color: #9E8E7E; }")
            .arg(layout::scaled(6, win))
            .arg(qMax(8, layout::scaled(11, win)));
    }

    inline QString primary_button(const QSize win)
    {
        return QStringLiteral(
            "QPushButton"
            "{"
            "    background: #2FB4E0;"
            "    border: none;"
            "    border-radius: %1px;"
            "    color: #FFFFFF;"
            "    font-family: 'Eurostile';"
            "    font-weight: 900;"
            "    font-size: %2px;"
            "}"
            "QPushButton:hover { background: #4FC4EF; }"
            "QPushButton:pressed { background: #168EB8; }"
            "QPushButton:disabled { background: #D8CDC0; color: #9E8E7E; }")
            .arg(layout::scaled(6, win))
            .arg(qMax(8, layout::scaled(11, win)));
    }

    inline QString link_blue(const QSize win)
    {
        return QStringLiteral(
            "QPushButton"
            "{"
            "    background: transparent;"
            "    border: none;"
            "    color: #2FB4E0;"
            "    font-family: 'Inter';"
            "    font-size: %1px;"
            "    font-weight: bold;"
            "    text-decoration: underline;"
            "}"
            "QPushButton:hover { color: #6FD4EF; }")
            .arg(qMax(9, layout::scaled(13, win)));
    }

    inline QString link_blue_lg(const QSize win)
    {
        return QStringLiteral(
            "QPushButton"
            "{"
            "    background: transparent;"
            "    border: none;"
            "    color: #2FB4E0;"
            "    font-family: 'Inter';"
            "    font-size: %1px;"
            "    text-decoration: underline;"
            "}"
            "QPushButton:hover { color: #6FD4EF; }")
            .arg(qMax(10, layout::scaled(15, win)));
    }

    inline constexpr const char* k_flat_transparent =
        "border:none; background:transparent;";
}
