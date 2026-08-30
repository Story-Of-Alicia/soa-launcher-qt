#include "ui/ImageDropdown.hpp"

#include <QFocusEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPoint>

#include <utility>

#include "ui/Assets.hpp"
#include "ui/Layout.hpp"
#include "i18n/LanguageManager.hpp"

namespace dd = util::layout::dropdown;

ImageDropdown::ImageDropdown(QStringList options, QWidget* parent)
    : QWidget(parent), items(std::move(options))
{
    if (items.isEmpty())
    {
        items.push_back(QStringLiteral("Unavailable"));
        setEnabled(false);
    }

    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setAccessibleName(QStringLiteral("Selection menu"));
    setAccessibleDescription(items.value(current));
    setFixedSize(dd::box(window()->size()));
    connect(&util::i18n::LanguageManager::instance(),
            &util::i18n::LanguageManager::language_changed,
            this, [this]()
    {
        setAccessibleName(util::i18n::translate("Selection menu"));
        setAccessibleDescription(util::i18n::translate(items.value(current)));
        update();
    });
}

QRect ImageDropdown::closed_rect() const
{
    const QSize w = window()->size();
    const QSize box = dd::box(w);
    if (!open || !opens_upward)
        return dd::closed_rect(w);
    const QSize total = dd::total_size(w, items.size());
    return {0, total.height() - box.height(), box.width(), box.height()};
}

QRect ImageDropdown::option_rect(const int slot) const
{
    if (!open || !opens_upward)
        return dd::option_rect(window()->size(), slot);
    const QSize w = window()->size();
    const QSize box = dd::box(w);
    const int step = dd::option_h(w) - dd::option_overlap(w);
    return {0, slot * step, box.width(), dd::option_h(w)};
}

void ImageDropdown::set_open_upwards(const bool value)
{
    if (open)
        set_open(false);
    opens_upward = value;
}

void ImageDropdown::set_open(const bool value)
{
    if (!isEnabled() || open == value)
        return;

    const QSize w = window()->size();
    const QSize box = dd::box(w);
    if (value)
    {
        closed_position = pos();
        const QSize total = dd::total_size(w, items.size());
        open = true;
        if (opens_upward)
            move(closed_position.x(), closed_position.y() - total.height() + box.height());
        setFixedSize(total);
        raise();
    }
    else
    {
        open = false;
        hovered_slot = -1;
        setFixedSize(box);
        if (opens_upward)
            move(closed_position);
    }
    update();
}

void ImageDropdown::set_index(const int i)
{
    if (i < 0 || i >= items.size() || i == current)
        return;
    current = i;
    setAccessibleDescription(util::i18n::translate(items.value(current)));
    update();
    emit changed(current);
}

void ImageDropdown::select_relative(const int delta)
{
    if (items.size() < 2)
        return;
    const int next = (current + delta + items.size()) % items.size();
    set_index(next);
}

void ImageDropdown::mousePressEvent(QMouseEvent* event)
{
    if (closed_rect().contains(event->pos()))
    {
        setFocus(Qt::MouseFocusReason);
        set_open(!open);
        event->accept();
        return;
    }

    if (open)
    {
        int slot = 0;
        for (int i = 0; i < items.size(); ++i)
        {
            if (i == current)
                continue;
            if (option_rect(slot).contains(event->pos()))
            {
                current = i;
                setAccessibleDescription(util::i18n::translate(items.value(current)));
                set_open(false);
                emit changed(current);
                event->accept();
                return;
            }
            ++slot;
        }
        set_open(false);
    }
    QWidget::mousePressEvent(event);
}


void ImageDropdown::mouseMoveEvent(QMouseEvent* event)
{
    int next_hovered = -1;
    if (open)
    {
        int slot = 0;
        for (int i = 0; i < items.size(); ++i)
        {
            if (i == current)
                continue;
            if (option_rect(slot).contains(event->pos()))
            {
                next_hovered = slot;
                break;
            }
            ++slot;
        }
    }

    if (hovered_slot != next_hovered)
    {
        hovered_slot = next_hovered;
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void ImageDropdown::leaveEvent(QEvent* event)
{
    if (hovered_slot != -1)
    {
        hovered_slot = -1;
        update();
    }
    QWidget::leaveEvent(event);
}

void ImageDropdown::keyPressEvent(QKeyEvent* event)
{
    switch (event->key())
    {
        case Qt::Key_Up:
        case Qt::Key_Left:
            select_relative(-1);
            event->accept();
            return;
        case Qt::Key_Down:
        case Qt::Key_Right:
            select_relative(1);
            event->accept();
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
            set_open(!open);
            event->accept();
            return;
        case Qt::Key_Escape:
            set_open(false);
            event->accept();
            return;
        default:
            QWidget::keyPressEvent(event);
            return;
    }
}

void ImageDropdown::focusOutEvent(QFocusEvent* event)
{
    set_open(false);
    QWidget::focusOutEvent(event);
}

void ImageDropdown::paintEvent(QPaintEvent*)
{
    const QSize w = window()->size();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setOpacity(isEnabled() ? 1.0 : 0.48);

    const QPixmap& dropdown_px = util::assets::images[util::assets::Image::MenuDropdown];
    const int lip = dd::pad_bottom(w);
    const int pad = dd::text_pad(w);

    QFont font = util::assets::fonts[util::assets::Font::Inter];
    font.setPixelSize(util::layout::scaled(util::layout::text::k_label, w));
    font.setWeight(QFont::Medium);
    painter.setFont(font);
    const QColor text_col {0x4F, 0x17, 0x17};

    if (open)
    {
        int slot = 0;
        for (int i = 0; i < items.size(); ++i)
        {
            if (i == current)
                continue;
            const QRect rect = option_rect(slot++);
            painter.drawPixmap(rect, dropdown_px);
            if (slot - 1 == hovered_slot)
            {
                painter.fillRect(rect.adjusted(util::layout::scaled(5, w),
                                               util::layout::scaled(4, w),
                                               -util::layout::scaled(5, w),
                                               -util::layout::scaled(8, w)),
                                 QColor(47, 180, 224, 34));
            }
            painter.setPen(text_col);
            const QRect text_rect = rect.adjusted(pad, 0, -pad, -lip);
            const QString translated = util::i18n::translate(items[i]);
            painter.drawText(text_rect,
                             Qt::AlignVCenter | Qt::AlignLeft,
                             painter.fontMetrics().elidedText(
                                 translated, Qt::ElideRight, text_rect.width()));
        }
    }

    const QRect closed = closed_rect();
    painter.drawPixmap(closed, dropdown_px);
    painter.setPen(text_col);
    const QRect closed_text = closed.adjusted(
        pad, 0, -util::layout::scaled(52, w), -lip);
    const QString current_text = util::i18n::translate(items.value(current));
    painter.drawText(closed_text,
                     Qt::AlignVCenter | Qt::AlignLeft,
                     painter.fontMetrics().elidedText(
                         current_text, Qt::ElideRight, closed_text.width()));

    painter.setPen(QPen(QColor(0xA8, 0x90, 0x78), util::layout::scaled(2, w)));
    const QPoint center = dd::chevron_center(w) + QPoint(0, closed.top());
    const int arm = dd::chevron_arm(w);
    if (open)
    {
        painter.drawLine(center.x() - arm, center.y() + arm / 2, center.x(), center.y() - arm / 2);
        painter.drawLine(center.x(), center.y() - arm / 2, center.x() + arm, center.y() + arm / 2);
    }
    else
    {
        painter.drawLine(center.x() - arm, center.y() - arm / 2, center.x(), center.y() + arm / 2);
        painter.drawLine(center.x(), center.y() + arm / 2, center.x() + arm, center.y() - arm / 2);
    }

}
