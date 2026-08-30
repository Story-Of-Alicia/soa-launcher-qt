#include "ui/ProgressBar.hpp"
#include "ui/Assets.hpp"

#include <QPainter>
#include <QRect>
#include <QtGlobal>

namespace util::progress_bar
{
    namespace
    {
        QRect fill_area(const QRect& bar)
        {
            const int inset_x = qMax(1, qRound(9.0 * bar.width() / 430.0));
            const int inset_y = qMax(1, qRound(1.0 * bar.height() / 19.0));
            return bar.adjusted(inset_x, inset_y, -inset_x, -inset_y);
        }

        void draw_segment(QPainter& painter, const QRect& area,
                          const int left, const int width)
        {
            if (area.isEmpty() || width <= 0)
                return;

            const QPixmap& start_px = assets::images[assets::Image::ProgressBarStart];
            const QPixmap& mid_px = assets::images[assets::Image::ProgressBarMiddle];
            const QPixmap& end_px = assets::images[assets::Image::ProgressBarEnd];
            const int height = area.height();
            const int start_width = start_px.height() > 0
                ? qRound(start_px.width() * height / double(start_px.height()))
                : height;
            const int end_width = end_px.height() > 0
                ? qRound(end_px.width() * height / double(end_px.height()))
                : height;

            painter.save();
            painter.setClipRect(area.intersected(QRect(left, area.top(), width, height)));
            painter.drawPixmap(QRect(left, area.top(), start_width, height), start_px);

            const int middle_left = left + start_width;
            const int middle_right = left + width - end_width;
            const int middle_width = qMax(0, middle_right - middle_left);
            if (middle_width > 0)
                painter.drawPixmap(
                    QRect(middle_left, area.top(), middle_width, height), mid_px);

            if (width > start_width)
            {
                const int visible_end_width = qMin(end_width, width);
                painter.drawPixmap(
                    QRect(left + width - visible_end_width, area.top(),
                          visible_end_width, height),
                    end_px);
            }
            painter.restore();
        }
    }

    void draw(QPainter& painter, const QRect& bar, const double fraction)
    {
        painter.drawPixmap(bar, assets::images[assets::Image::ProgressBarTrack]);
        if (fraction <= 0.0 || bar.isEmpty())
            return;
        const QRect area = fill_area(bar);
        draw_segment(painter, area, area.left(),
                     qRound(area.width() * qBound(0.0, fraction, 1.0)));
    }

    void draw_indeterminate(QPainter& painter, const QRect& bar, const double phase)
    {
        painter.drawPixmap(bar, assets::images[assets::Image::ProgressBarTrack]);
        if (bar.isEmpty())
            return;
        const QRect area = fill_area(bar);
        const int segment_width = qMax(1, qRound(area.width() * 0.28));
        const int travel = area.width() + segment_width;
        const int left = area.left() - segment_width
            + qRound(travel * qBound(0.0, phase, 1.0));
        draw_segment(painter, area, left, segment_width);
    }
}
