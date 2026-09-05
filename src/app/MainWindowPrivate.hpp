#pragma once

#include "config/Config.hpp"
#include "ui/Assets.hpp"
#include "ui/LauncherDialog.hpp"
#include "ui/Layout.hpp"

#include <QGuiApplication>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QStringList>

namespace app::detail
{
    inline LauncherDialog* show_modeless_message(QWidget* parent,
                                                  const LauncherDialog::Tone tone,
                                                  const QString& title,
                                                  const QString& message,
                                                  const QString& informative = {})
    {
        return LauncherDialog::open_message(parent, tone, title, message, informative);
    }

    inline QSize configured_window_size()
    {
        const QString configured = util::config::Config::instance().launcher_size();
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

    inline QPixmap make_version_button(const QSize window_size, const QRect button_rect,
                                       const QPixmap& frame, const QPixmap& icon,
                                       const QPoint icon_offset)
    {
        QPixmap composed(button_rect.size());
        composed.fill(Qt::transparent);

        QPainter painter(&composed);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        const QSize icon_size = util::layout::scaled(icon.size(), window_size);
        painter.drawPixmap(icon_offset,
                           icon.scaled(icon_size, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation));
        painter.drawPixmap(QRect(QPoint(0, 0), button_rect.size()), frame);
        return composed;
    }
}
