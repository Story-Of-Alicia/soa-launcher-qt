#pragma once

#include <QColor>
#include <QEvent>
#include <QFont>
#include <QLabel>
#include <QPoint>
#include <QPushButton>

#include "ui/Assets.hpp"

namespace util::simple_utils
{
    void make_label_block(QWidget* parent, QSize window_size, int y,
                          const QString& title, const QString& description);
    QPushButton* make_flat_button(QWidget* parent);
    bool apply_button_state(QEvent* event, QPushButton* button,
                            const QPixmap& normal, const QPixmap& hover,
                            const QPixmap& clicked);
    QLabel* add_button_text(QPushButton* button, assets::Button asset,
                            const QString& source, const QFont& font,
                            const QColor& color = QColor(Qt::white),
                            QPoint offset = {});
    void set_button_asset(QPushButton* button, assets::Button asset);
    void set_button_loading(QPushButton* button, bool loading);
    void set_button_enabled(QPushButton* button, bool enabled);
    void set_button_text(QPushButton* button, const QString& source);
    void refresh_button(QPushButton* button);
}
