#pragma once

#include <QObject>
#include <QPoint>

class QActionGroup;
class QFrame;
class QMenu;
class QPushButton;
class QToolButton;
class QWidget;

class LauncherMenuController final : public QObject
{
    Q_OBJECT

public:
    explicit LauncherMenuController(QWidget* host, QObject* parent = nullptr);

    [[nodiscard]] bool is_visible() const;
    [[nodiscard]] bool contains(const QPoint& host_position) const;
    void set_visible(bool visible);
    void raise_controls(bool chrome_hidden);
    void set_manage_versions_enabled(bool enabled);
    void retranslate();

signals:
    void show_log_requested();
    void manage_versions_requested();
    void credits_requested();
    void about_requested();

private:
    void refresh_language_actions();

    QWidget* host {};
    QToolButton* menu_button {};
    QFrame* menu_panel {};
    QMenu* language_menu {};
    QActionGroup* language_action_group {};
    QPushButton* language_button {};
    QPushButton* show_log_button {};
    QPushButton* manage_versions_button {};
    QPushButton* credits_button {};
    QPushButton* about_button {};
};
