#pragma once
#include <QRect>
#include <QSize>
#include <QPoint>
#include <QtGlobal>

namespace util::layout
{
    namespace win
    {
        inline constexpr QSize k_small   {1120, 677};
        inline constexpr QSize k_default {1400, 846};
        inline constexpr QSize k_large   {1600, 967};
        inline constexpr QSize k_4k      {1920, 1160};
    }

    double scale(QSize win);
    int    scaled(int v, QSize win);
    QSize  scaled(QSize s, QSize win);
    QPoint scaled(QPoint p, QSize win);
    QRect  scaled(QRect r, QSize win);

    namespace region
    {
        inline constexpr int k_left   = 30;
        inline constexpr int k_top    = 22;
        inline constexpr int k_right  = 20;
        inline constexpr int k_bottom = 30;
        inline constexpr int k_radius = 35;

        inline constexpr QRect k_default
        {
            k_left,
            k_top,
            win::k_default.width()  - k_left - k_right,
            win::k_default.height() - k_top  - k_bottom
        };

        QRect rect(QSize win);
    }







    namespace radius
    {
        inline constexpr int k_panel   = 0;
        inline constexpr int k_card    = 0;
        inline constexpr int k_control = 6;
    }

    constexpr QRect center_in_region(const QSize box, const int dx = 0, const int dy = 0)
    {
        return
        {
            region::k_default.x() + (region::k_default.width()  - box.width())  / 2 + dx,
            region::k_default.y() + (region::k_default.height() - box.height()) / 2 + dy,
            box.width(), box.height()
        };
    }

    constexpr QRect hcenter_in_region(const QSize box, const int y)
    {
        return
        {
            region::k_default.x() + (region::k_default.width() - box.width()) / 2,
            y,
            box.width(), box.height()
        };
    }

    constexpr QRect anchor_top_right(const QRect parent, const int from_right, const int from_top, const QSize sz)
    {
        return
        {
            parent.right() + 1 - from_right - sz.width(),
            parent.top() + from_top,
            sz.width(), sz.height()
        };
    }

    constexpr QRect anchor_bottom_right(const QRect parent, const int from_right, const int from_bottom, const QSize sz)
    {
        return
        {
            parent.right()  + 1 - from_right  - sz.width(),
            parent.bottom() + 1 - from_bottom - sz.height(),
            sz.width(), sz.height()
        };
    }

    QRect centered(QSize box, QSize win, int dx = 0, int dy = 0);





    namespace modal_close
    {
        inline constexpr QSize k_icon {16, 16};
        inline constexpr QSize k_hit  {40, 40};
        inline constexpr int   k_icon_from_right = 39;
        inline constexpr int   k_icon_from_top   = 34;

        constexpr QRect rect_in(const QRect panel)
        {
            return anchor_top_right(panel,
                                    k_icon_from_right - (k_hit.width() - k_icon.width()) / 2,
                                    k_icon_from_top - (k_hit.height() - k_icon.height()) / 2,
                                    k_hit);
        }

        inline QRect rect_in(const QRect panel, const QSize win)
        {
            const QSize hit = scaled(k_hit, win);
            const QSize icon = scaled(k_icon, win);
            const int from_right = scaled(k_icon_from_right, win)
                - (hit.width() - icon.width()) / 2;
            const int from_top = scaled(k_icon_from_top, win)
                - (hit.height() - icon.height()) / 2;
            return anchor_top_right(panel, from_right, from_top, hit);
        }



        constexpr QRect rect_left_of(const QRect panel, const int steps = 1)
        {
            const QRect close_rect = rect_in(panel);
            return {close_rect.x() - steps * k_hit.width(), close_rect.y(),
                    k_hit.width(), k_hit.height()};
        }
    }

    namespace text
    {
        inline constexpr int k_modal_header = 25;
        inline constexpr int k_row_title    = 20;
        inline constexpr int k_banner       = 18;
        inline constexpr int k_label        = 15;
        inline constexpr int k_desc         = 14;
        inline constexpr int k_body         = 13;
        inline constexpr int k_version      = 16;
        inline constexpr int k_status       = 12;
    }

    namespace chrome
    {
        inline constexpr QRect k_menu          {55, 34, 48, 44};
        inline constexpr QRect k_close         {1315, 40, 40, 40};
        inline constexpr QSize k_close_icon    {35, 35};
        inline constexpr QRect k_minimize      {1270, 37, 50, 50};
        inline constexpr QSize k_minimize_icon {50, 50};

        inline constexpr QRect k_playtest_button {56, 495, 113, 113};
        inline constexpr QRect k_alicia_2_button {58, 613, 113, 113};
        inline constexpr QPoint k_playtest_icon_offset {10, 10};
        inline constexpr QPoint k_alicia_2_icon_offset {12, 8};

        inline constexpr QRect k_version =
            anchor_bottom_right(region::k_default, 35, 33, {245, 20});
        inline constexpr QRect k_version_art =
            anchor_bottom_right(region::k_default, 35, 58, {150, 125});

        QRect  menu(QSize win);
        QRect  close(QSize win);
        QSize  close_icon(QSize win);
        QRect  minimize(QSize win);
        QSize  minimize_icon(QSize win);

        QRect playtest_button(QSize win);
        QRect alicia_2_button(QSize win);
        QPoint playtest_icon_offset(QSize win);
        QPoint alicia_2_icon_offset(QSize win);

        QRect  version(QSize win);
        QRect  version_art(QSize win);
    }

    namespace alicia_chooser
    {
        inline constexpr QSize k_box        {640, 360};
        inline constexpr int   k_top_offset = 365;
        inline constexpr QRect k_rect = hcenter_in_region(k_box, k_top_offset);

        inline constexpr QRect k_title {0, 38, k_box.width(), 30};
        inline constexpr QRect k_settings_button =
            anchor_top_right({0, 0, k_box.width(), k_box.height()}, 25, 23, {40, 40});
        inline constexpr QRect k_reset {0, 303, k_box.width(), 40};

        inline constexpr QRect k_message {95, 92, 430, 70};
        inline constexpr int   k_dl_button_x = k_message.x();
        inline constexpr int   k_dl_button_y = 187;
        inline constexpr int   k_dl_button_w = k_message.width();

        inline constexpr QSize k_discord_icon   {24, 24};
        inline constexpr QRect k_discord_button  {105, 100, 430, 56};
        inline constexpr QRect k_keep_signed_in  {104, 157, 260, 40};
        inline constexpr QRect k_disclaimer      {105, 198, 430, 102};

        inline constexpr QRect k_waiting_title {0, 100, k_box.width(), 28};
        inline constexpr QRect k_steps         {85, 148, 470, 108};
        inline constexpr QRect k_try_again     {0, 258, k_box.width(), 40};

        inline constexpr QRect k_signed_bug_checkbox {104, 88, 24, 28};
        inline constexpr QRect k_signed_bug_text     {134, 88, 402, 28};
        inline constexpr QRect k_signed_rules_checkbox {104, 116, 24, 28};
        inline constexpr QRect k_signed_rules_text     {134, 116, 402, 28};
        inline constexpr QRect k_signed_in_banner {105, 150, 430, 48};
        inline constexpr QRect k_enter_button     {105, 211, 430, 67};

        QRect  rect(QSize win);
        QPoint pos(QSize win);
        QSize  box(QSize win);
        QRect  title(QSize win);
        QRect  settings_button(QSize win);
        QRect  reset(QSize win);

        QRect  message(QSize win);
        int    dl_button_x(QSize win);
        int    dl_button_y(QSize win);
        int    dl_button_w(QSize win);

        QSize  discord_icon(QSize win);
        QRect  discord_button(QSize win);
        QRect  keep_signed_in(QSize win);
        QRect  disclaimer(QSize win);

        QRect  waiting_title(QSize win);
        QRect  steps(QSize win);
        QRect  try_again(QSize win);

        QRect  signed_bug_checkbox(QSize win);
        QRect  signed_bug_text(QSize win);
        QRect  signed_rules_checkbox(QSize win);
        QRect  signed_rules_text(QSize win);
        QRect  signed_in_banner(QSize win);
        QRect  enter_button(QSize win);
    }

    namespace settings
    {
        inline constexpr QSize k_box {630, 570};
        inline constexpr QSize k_box_expanded {630, 620};
        inline constexpr QSize k_close_icon = modal_close::k_icon;
        inline constexpr QSize k_close_hit  = modal_close::k_hit;
        inline constexpr int   k_margin_top = 45;
        inline constexpr int   k_margin_top_expanded = 25;
        inline constexpr int   k_header_gap = 60;
        inline constexpr int   k_row_gap = 50;
        inline constexpr int   k_padding = 37;

        inline constexpr QSize k_tab        {150, 44};
        inline constexpr int   k_tab_radius = 12;
        inline constexpr int   k_tab_gap    = 6;
        inline constexpr int   k_tab_inset  = 40;
        inline constexpr int   k_tab_overlap = 4;

        inline constexpr QRect k_page_title {0, 34, k_box.width(), 30};
        inline constexpr int   k_control_col = 227;
        inline constexpr QSize k_slider      {76, 40};
        inline constexpr int   k_slider_gap  = 10;
        inline constexpr int   k_desc_max_w  = 270;

        inline constexpr int   k_text_x    = 37;
        inline constexpr int   k_text_w    = 290;
        inline constexpr int   k_ctrl_x    = 366;
        inline constexpr int   k_ctrl_w    = 227;
        inline constexpr int   k_desc_dy   = 30;
        inline constexpr int   k_desc_h    = 46;
        inline constexpr int   k_title_h   = 28;



        inline constexpr int   k_row_top         = 82;
        inline constexpr int   k_row_bottom      = 455;
        inline constexpr int   k_row_bottom_foot = 410;

        inline constexpr int   k_input_h    = 40;
        inline constexpr int   k_browse_w   = 40;
        inline constexpr int   k_input_gap  = 6;

        QRect box_rect(QSize win, bool expanded = false);
        QSize box(QSize win, bool expanded = false);
        QRect close(QSize win, bool expanded = false);
        QSize close_icon(QSize win);
        int   header_gap(QSize win);
        int   row_gap(QSize win);
        int   control_col(QSize win);
        QSize slider(QSize win);
        int   desc_max_w(QSize win);

        QSize tab(QSize win);
        int   tab_gap(QSize win);
        int   tab_inset(QSize win);
        int   tab_overlap(QSize win);
        QRect tab_rect(QSize win, int i, bool expanded = false);
        int   tab_radius(QSize win);

        QRect row_title(QSize win, int y);
        QRect row_desc(QSize win, int y);
        int   ctrl_x(QSize win);
        int   ctrl_w(QSize win);
        QPoint ctrl_pos(QSize win, int y);
        QRect run_check(QSize win, int y);
        QRect slider_rect(QSize win, int y);
        QRect page_title(QSize win, bool expanded = false);

        int   row_y(int index, int count, bool has_footer = false);
        QRect field_rect(QSize win, int y);
        QRect browse_rect(QSize win, int y);
    }

    namespace launcher_settings
    {
        int   row(int i, bool expanded = false);
        QRect connectivity_results(QSize win);
        QRect connectivity_text(QSize win);
        QRect copy_report(QSize win);
    }

    namespace wine_settings
    {
        int row(int i);
    }

    namespace advanced_settings
    {
        inline constexpr int k_row_single = 78;
        int row(int i = 0, int count = 3);
    }

    namespace dropdown
    {
        inline constexpr QSize k_box            {227, 64};
        inline constexpr int   k_option_h       = 64;
        inline constexpr int   k_option_overlap = 21;
        inline constexpr int   k_pad_bottom     = 10;
        inline constexpr int   k_text_pad       = 20;
        inline constexpr int   k_chevron_inset  = 28;
        inline constexpr int   k_chevron_arm    = 5;

        QSize box(QSize win);
        int   option_h(QSize win);
        int   option_overlap(QSize win);

        QSize  total_size(QSize win, int count);
        QRect  closed_rect(QSize win);
        QRect  option_rect(QSize win, int slot);
        int    text_pad(QSize win);
        int    pad_bottom(QSize win);
        QPoint chevron_center(QSize win);
        int    chevron_arm(QSize win);
    }

    namespace install_modal
    {
        inline constexpr QSize k_box        {580, 382};
        inline constexpr int   k_margin_top = 66;

        inline constexpr int   k_title_y      = 40;
        inline constexpr int   k_title_h      = 30;
        inline constexpr int   k_body_y       = 100;
        inline constexpr int   k_body_h       = 40;
        inline constexpr int   k_text_x       = 33;
        inline constexpr int   k_path_inset   = 33;
        inline constexpr int   k_path_h       = 69;
        inline constexpr int   k_path_y       = 150;
        inline constexpr int   k_changepath_y = 252;
        inline constexpr int   k_changepath_h = 20;
        inline constexpr int   k_warn_y       = 224;
        inline constexpr int   k_warn_h       = 18;
        inline constexpr int   k_button_row_y = 287;
        inline constexpr int   k_button_h     = 40;
        inline constexpr int   k_bottom_pad   = 55;
        inline constexpr int   k_button_outer = 70;
        inline constexpr int   k_button_gap   = 35;

        inline constexpr QRect k_rect  = center_in_region(k_box, 0, k_margin_top);
        inline constexpr QSize k_close_icon = modal_close::k_icon;
        inline constexpr QSize k_close_hit  = modal_close::k_hit;
        inline constexpr QRect k_close =
            modal_close::rect_in({0, 0, k_box.width(), k_box.height()});

        inline constexpr QRect k_title {0, k_title_y, k_box.width(), k_title_h};
        inline constexpr QRect k_body  {k_text_x, k_body_y, k_box.width() - 2 * k_text_x, k_body_h};
        inline constexpr QRect k_path  {k_path_inset, k_path_y, k_box.width() - 2 * k_path_inset, k_path_h};
        inline constexpr QRect k_changepath {k_path_inset, k_changepath_y, k_box.width() - 2 * k_path_inset, k_changepath_h};
        inline constexpr QRect k_warn        {k_path_inset, k_warn_y, k_box.width() - 2 * k_path_inset, k_warn_h};

        inline constexpr int   k_btn_w   = (k_box.width() - 2 * k_button_outer - k_button_gap) / 2;
        inline constexpr QRect k_cancel  {k_button_outer, k_button_row_y, k_btn_w, k_button_h};
        inline constexpr QRect k_install {k_button_outer + k_btn_w + k_button_gap, k_button_row_y,
                                          k_box.width() - k_button_outer - (k_button_outer + k_btn_w + k_button_gap), k_button_h};

        QRect  box_rect(QSize win);
        QSize  box(QSize win);
        QRect  rect(QSize win);
        QRect  close(QSize win);
        QRect  title(QSize win);
        QRect  body(QSize win);
        QRect  path_field(QSize win);
        QRect  changepath_line(QSize win);
        QRect  change_path_button(QSize win);
        QRect  warning_line(QSize win);
        QRect  cancel_button(QSize win);
        QRect  install_button(QSize win);
        QSize  close_icon(QSize win);
    }

    namespace progress_modal
    {
        inline constexpr QSize k_box        {490, 210};
        inline constexpr int   k_margin_top = 40;

        inline constexpr int   k_pad_x       = 30;
        inline constexpr int   k_title_y     = 24;
        inline constexpr int   k_title_h     = 30;
        inline constexpr int   k_info_y      = 74;
        inline constexpr int   k_info_h      = 18;
        inline constexpr int   k_bar_y       = 100;
        inline constexpr int   k_bar_h       = 19;
        inline constexpr int   k_under_y     = 129;
        inline constexpr int   k_under_h     = 20;

        inline constexpr QRect k_rect = center_in_region(k_box, 0, k_margin_top);
        inline constexpr QSize k_close_icon = modal_close::k_icon;
        inline constexpr QSize k_close_hit  = modal_close::k_hit;
        inline constexpr QRect k_close =
            modal_close::rect_in({0, 0, k_box.width(), k_box.height()});
        inline constexpr QSize k_pause_hit  = modal_close::k_hit;


        inline constexpr QRect k_pause =
            modal_close::rect_left_of({0, 0, k_box.width(), k_box.height()});

        inline constexpr QRect k_title {0, k_title_y, k_box.width(), k_title_h};
        inline constexpr QRect k_info  {k_pad_x, k_info_y,  k_box.width() - 2 * k_pad_x, k_info_h};
        inline constexpr QRect k_bar   {k_pad_x, k_bar_y,   k_box.width() - 2 * k_pad_x, k_bar_h};
        inline constexpr QRect k_under {k_pad_x, k_under_y, k_box.width() - 2 * k_pad_x, k_under_h};

        QRect box_rect(QSize win);
        QSize box(QSize win);
        QRect rect(QSize win);
        QRect close(QSize win);
        QSize close_icon(QSize win);
        QRect pause(QSize win);
        QSize pause_icon(QSize win);
        QRect title(QSize win);
        QRect info_row(QSize win);
        QRect bar_rect(QSize win);
        QRect under_row(QSize win);
        QRect retry_button(QSize win);
        QRect details_button(QSize win);
    }
}
