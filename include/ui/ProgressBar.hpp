#pragma once

class QPainter;
class QRect;

namespace util::progress_bar
{
    void draw(QPainter& painter, const QRect& bar, double fraction);
    void draw_indeterminate(QPainter& painter, const QRect& bar, double phase);
}
