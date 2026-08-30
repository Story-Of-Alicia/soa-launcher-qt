#pragma once

#include <QDialog>
#include <QPoint>

class QLabel;
class QMouseEvent;
class QPushButton;
class QShowEvent;
class QWidget;

class LauncherInfoDialog final : public QDialog
{
public:
    enum class Page
    {
        About,
        Credits
    };

    explicit LauncherInfoDialog(Page page, QWidget* parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void setup_ui();
    void retranslate();

    Page page;
    bool dragging {};
    QPoint drag_offset;
    QLabel* logo_label {};
    QLabel* title_label {};
    QLabel* subtitle_label {};
    QLabel* badge_label {};
    QLabel* info_label {};
    QPushButton* website_button {};
    QPushButton* source_button {};
    QPushButton* contact_button {};
    QPushButton* close_button {};
    QWidget* links_widget {};
};
