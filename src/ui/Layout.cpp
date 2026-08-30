#include "ui/Layout.hpp"

namespace util::layout
{
    double scale(const QSize win)
    {
        const double sx = static_cast<double>(win.width()) / win::k_default.width();
        const double sy = static_cast<double>(win.height()) / win::k_default.height();
        return qMin(sx, sy);
    }

    int scaled(const int v, const QSize win)
    {
        return qRound(v * scale(win));
    }

    QSize scaled(const QSize s, const QSize win)
    {
        const double f = scale(win);
        return { qRound(s.width() * f), qRound(s.height() * f) };
    }

    QPoint scaled(const QPoint p, const QSize win)
    {
        const double f = scale(win);
        return { qRound(p.x() * f), qRound(p.y() * f) };
    }

    QRect scaled(const QRect r, const QSize win)
    {
        const double f = scale(win);
        return
        {
            qRound(r.x() * f),
            qRound(r.y() * f),
            qRound(r.width()  * f),
            qRound(r.height() * f)
        };
    }

    QRect centered(const QSize box, const QSize win, const int dx, const int dy)
    {
        return scaled(center_in_region(box, dx, dy), win);
    }

    namespace region
    {
        QRect rect(const QSize win) { return scaled(k_default, win); }
    }

    namespace chrome
    {
        QRect menu(const QSize win)          { return scaled(k_menu, win); }
        QRect close(const QSize win)         { return scaled(k_close, win); }
        QSize close_icon(const QSize win)    { return scaled(k_close_icon, win); }
        QRect minimize(const QSize win)      { return scaled(k_minimize, win); }
        QSize minimize_icon(const QSize win) { return scaled(k_minimize_icon, win); }

        QRect playtest_button(const QSize win) { return scaled(k_playtest_button, win); }
        QRect alicia_2_button(const QSize win) { return scaled(k_alicia_2_button, win); }
        QPoint playtest_icon_offset(const QSize win) { return scaled(k_playtest_icon_offset, win); }
        QPoint alicia_2_icon_offset(const QSize win) { return scaled(k_alicia_2_icon_offset, win); }

        QRect version(const QSize win) { return scaled(k_version, win); }
        QRect version_art(const QSize win) { return scaled(k_version_art, win); }
    }

    namespace alicia_chooser
    {
        QRect  rect(const QSize win)            { return scaled(k_rect, win); }
        QPoint pos(const QSize win)             { return rect(win).topLeft(); }
        QSize  box(const QSize win)             { return scaled(k_box, win); }
        QRect  title(const QSize win)           { return scaled(k_title, win); }
        QRect  settings_button(const QSize win) { return scaled(k_settings_button, win); }
        QRect  reset(const QSize win)           { return scaled(k_reset, win); }

        QRect  message(const QSize win)         { return scaled(k_message, win); }
        int    dl_button_x(const QSize win)     { return scaled(k_dl_button_x, win); }
        int    dl_button_y(const QSize win)     { return scaled(k_dl_button_y, win); }
        int    dl_button_w(const QSize win)     { return scaled(k_dl_button_w, win); }

        QSize  discord_icon(const QSize win)    { return scaled(k_discord_icon, win); }
        QRect  discord_button(const QSize win)  { return scaled(k_discord_button, win); }
        QRect  keep_signed_in(const QSize win)  { return scaled(k_keep_signed_in, win); }
        QRect  disclaimer(const QSize win)      { return scaled(k_disclaimer, win); }

        QRect  waiting_title(const QSize win)   { return scaled(k_waiting_title, win); }
        QRect  steps(const QSize win)           { return scaled(k_steps, win); }
        QRect  try_again(const QSize win)       { return scaled(k_try_again, win); }

        QRect  signed_bug_checkbox(const QSize win) { return scaled(k_signed_bug_checkbox, win); }
        QRect  signed_bug_text(const QSize win) { return scaled(k_signed_bug_text, win); }
        QRect  signed_rules_checkbox(const QSize win) { return scaled(k_signed_rules_checkbox, win); }
        QRect  signed_rules_text(const QSize win) { return scaled(k_signed_rules_text, win); }
        QRect  signed_in_banner(const QSize win)  { return scaled(k_signed_in_banner, win); }
        QRect  enter_button(const QSize win)      { return scaled(k_enter_button, win); }
    }

    namespace settings
    {
        QRect base_box_rect(const bool expanded)
        {
            const QSize target = expanded ? k_box_expanded : k_box;
            const int offset = expanded ? k_margin_top_expanded : k_margin_top;
            return center_in_region(target, 0, offset);
        }

        QRect box_rect(const QSize win, const bool expanded)
        {
            return scaled(base_box_rect(expanded), win);
        }

        QSize box(const QSize win, const bool expanded)
        {
            return scaled(expanded ? k_box_expanded : k_box, win);
        }

        QRect close(const QSize win, const bool expanded)
        {
            return scaled(modal_close::rect_in(base_box_rect(expanded)), win);
        }

        QSize close_icon(const QSize win) { return scaled(k_close_icon, win); }
        int   header_gap(const QSize win) { return scaled(k_header_gap, win); }
        int   row_gap(const QSize win)    { return scaled(k_row_gap, win); }
        int   control_col(const QSize win){ return scaled(k_control_col, win); }
        QSize slider(const QSize win)     { return scaled(k_slider, win); }
        int   desc_max_w(const QSize win) { return scaled(k_desc_max_w, win); }

        QSize tab(const QSize win)        { return scaled(k_tab, win); }
        int   tab_gap(const QSize win)    { return scaled(k_tab_gap, win); }
        int   tab_inset(const QSize win)  { return scaled(k_tab_inset, win); }
        int   tab_overlap(const QSize win){ return scaled(k_tab_overlap, win); }

        QRect page_title(const QSize win, const bool expanded)
        {
            return scaled(k_page_title.translated(base_box_rect(expanded).topLeft()), win);
        }
        int   tab_radius(const QSize win) { return scaled(k_tab_radius, win); }

        QRect tab_rect(const QSize win, int i, const bool expanded)
        {
            const QRect box = box_rect(win, expanded);
            const QSize t = tab(win);
            const int gap = tab_gap(win);
            const int group_width = t.width() * 3 + gap * 2;
            const int x = box.left() + (box.width() - group_width) / 2
                + i * (t.width() + gap);
            const int y = box.top() - t.height() + tab_overlap(win);
            return {x, y, t.width(), t.height()};
        }

        QRect row_title(const QSize win, const int y)
        {
            return scaled(QRect{ k_text_x, y, k_text_w, k_title_h }, win);
        }

        QRect row_desc(const QSize win, const int y)
        {
            return scaled(QRect{ k_text_x, y + k_desc_dy, k_text_w, k_desc_h }, win);
        }

        int ctrl_x(const QSize win) { return scaled(k_ctrl_x, win); }
        int ctrl_w(const QSize win) { return scaled(k_ctrl_w, win); }

        QPoint ctrl_pos(const QSize win, const int y)
        {
            return scaled(QPoint{ k_ctrl_x, y }, win);
        }

        QRect run_check(const QSize win, const int y)
        {
            return scaled(QRect{ k_ctrl_x, y, k_ctrl_w, 48 }, win);
        }

        QRect slider_rect(const QSize win, const int y)
        {
            constexpr int x = k_ctrl_x + k_ctrl_w - k_slider.width();
            return scaled(QRect{ x, y, k_slider.width(), k_slider.height() }, win);
        }

        int row_y(const int index, const int count, const bool has_footer)
        {
            const int bottom = has_footer ? k_row_bottom_foot : k_row_bottom;
            if (count <= 1) return (k_row_top + bottom) / 2 - 30;
            return k_row_top + (index * (bottom - k_row_top)) / (count - 1);
        }

        QRect field_rect(const QSize win, const int y)
        {
            return scaled(QRect{ k_ctrl_x, y, k_ctrl_w - k_browse_w - k_input_gap, k_input_h }, win);
        }

        QRect browse_rect(const QSize win, const int y)
        {
            return scaled(QRect{ k_ctrl_x + k_ctrl_w - k_browse_w, y, k_browse_w, k_input_h }, win);
        }

    }

    namespace launcher_settings
    {
        int row(const int i, const bool expanded)
        {
            static constexpr int collapsed[4] = {72, 176, 280, 400};
            static constexpr int open[4] = {72, 176, 280, 475};
            if (i < 0 || i >= 4)
                return collapsed[0];
            return expanded ? open[i] : collapsed[i];
        }

        QRect connectivity_results(QSize win)
        {
            return scaled(QRect{settings::k_text_x, 358, 556, 104}, win);
        }

        QRect connectivity_text(QSize win)
        {
            const QRect panel = connectivity_results(win);
            const int inset = scaled(12, win);
            return QRect{inset, scaled(8, win),
                         panel.width() - inset * 2,
                         panel.height() - scaled(42, win)};
        }

        QRect copy_report(QSize win)
        {
            const QRect panel = connectivity_results(win);
            const int width = scaled(124, win);
            const int height = qMax(32, scaled(36, win));
            return QRect{panel.width() - width - scaled(10, win),
                         panel.height() - height - scaled(5, win),
                         width, height};
        }
    }

    namespace wine_settings
    {
        int row(const int i) { return settings::row_y(i, 5, false); }
    }

    namespace advanced_settings
    {
        int row(const int i, const int count)
        {
            static constexpr int ys_three[3] = {104, 218, 332};
            static constexpr int ys_four[4] = {82, 176, 270, 364};
            if (count == 3)
                return (i >= 0 && i < 3) ? ys_three[i] : ys_three[0];
            if (count == 4)
                return (i >= 0 && i < 4) ? ys_four[i] : ys_four[0];
            return settings::row_y(qBound(0, i, qMax(0, count - 1)),
                                   qMax(1, count), false);
        }
    }

    namespace dropdown
    {
        QSize box(const QSize win)            { return scaled(k_box, win); }
        int   option_h(const QSize win)       { return scaled(k_option_h, win); }
        int   option_overlap(const QSize win) { return scaled(k_option_overlap, win); }

        QSize total_size(const QSize win, const int count)
        {
            const QSize b = box(win);
            const int h = b.height() + (count - 1) * (option_h(win) - option_overlap(win));
            return { b.width(), h };
        }

        QRect closed_rect(const QSize win)
        {
            const QSize b = box(win);
            return { 0, 0, b.width(), b.height() };
        }

        QRect option_rect(const QSize win, const int slot)
        {
            const QSize b  = box(win);
            const int   oh = option_h(win);
            const int   ov = option_overlap(win);
            const int   y  = b.height() - ov + slot * (oh - ov);
            return { 0, y, b.width(), oh };
        }

        int text_pad(const QSize win)    { return scaled(k_text_pad, win); }
        int pad_bottom(const QSize win)  { return scaled(k_pad_bottom, win); }

        QPoint chevron_center(const QSize win)
        {
            const QRect c = closed_rect(win);
            return { c.right() - scaled(k_chevron_inset, win), c.center().y() };
        }

        int chevron_arm(const QSize win) { return scaled(k_chevron_arm, win); }
    }

    namespace install_modal
    {
        QRect  box_rect(const QSize win)         { return scaled(k_rect, win); }
        QSize  box(const QSize win)              { return scaled(k_box, win); }
        QRect  rect(const QSize win)             { return scaled(k_rect, win); }

        QRect  close(const QSize win)            { return scaled(k_close.translated(k_rect.topLeft()), win); }
        QRect  title(const QSize win)            { return scaled(k_title.translated(k_rect.topLeft()), win); }
        QRect  body(const QSize win)             { return scaled(k_body.translated(k_rect.topLeft()), win); }
        QRect  path_field(const QSize win)       { return scaled(k_path.translated(k_rect.topLeft()), win); }
        QRect  changepath_line(const QSize win)  { return scaled(k_changepath.translated(k_rect.topLeft()), win); }
        QRect  change_path_button(const QSize win)
        {
            const QRect row = changepath_line(win);
            const int hit_w = qMin(row.width() / 3, scaled(190, win));
            const int hit_h = qMax(32, row.height() + scaled(8, win));
            return { row.left(), row.center().y() - hit_h / 2, hit_w, hit_h };
        }
        QRect  warning_line(const QSize win)     { return scaled(k_warn.translated(k_rect.topLeft()), win); }
        QRect  cancel_button(const QSize win)    { return scaled(k_cancel.translated(k_rect.topLeft()), win); }
        QRect  install_button(const QSize win)   { return scaled(k_install.translated(k_rect.topLeft()), win); }
        QSize  close_icon(const QSize win)       { return scaled(k_close_icon, win); }
    }

    namespace progress_modal
    {
        QRect box_rect(const QSize win)  { return scaled(k_rect, win); }
        QSize box(const QSize win)       { return scaled(k_box, win); }
        QRect rect(const QSize win)      { return scaled(k_rect, win); }

        QRect close(const QSize win)     { return scaled(k_close.translated(k_rect.topLeft()), win); }
        QSize close_icon(const QSize win){ return scaled(k_close_icon, win); }
        QRect pause(const QSize win)     { return scaled(k_pause.translated(k_rect.topLeft()), win); }
        QSize pause_icon(const QSize win){ return scaled(k_pause_hit, win); }
        QRect title(const QSize win)     { return scaled(k_title.translated(k_rect.topLeft()), win); }
        QRect info_row(const QSize win)  { return scaled(k_info.translated(k_rect.topLeft()), win); }
        QRect bar_rect(const QSize win)  { return scaled(k_bar.translated(k_rect.topLeft()), win); }
        QRect under_row(const QSize win) { return scaled(k_under.translated(k_rect.topLeft()), win); }

        QRect retry_button(const QSize win)
        {
            const QRect under = under_row(win);
            const int width = scaled(118, win);
            const int height = qMax(32, scaled(38, win));
            const int gap = scaled(10, win);
            return { under.center().x() - gap / 2 - width,
                     under.bottom() + scaled(8, win),
                     width, height };
        }

        QRect details_button(const QSize win)
        {
            const QRect under = under_row(win);
            const int width = scaled(118, win);
            const int height = qMax(32, scaled(38, win));
            const int gap = scaled(10, win);
            return { under.center().x() + gap / 2,
                     under.bottom() + scaled(8, win),
                     width, height };
        }
    }
}
