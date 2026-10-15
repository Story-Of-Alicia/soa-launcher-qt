// Story of Alicia Launcher
// SPDX-FileCopyrightText: 2026 Story Of Alicia Contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional terms apply, see ADDITIONAL_TERMS.md.

#pragma once

class QPainter;
class QRect;

namespace soa::ui::progress_bar
{
    void draw(QPainter& painter, const QRect& bar, double fraction);
    void draw_indeterminate(QPainter& painter, const QRect& bar, double phase);
}
