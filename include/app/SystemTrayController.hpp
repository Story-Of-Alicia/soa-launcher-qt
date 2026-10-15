// Story of Alicia Launcher
// SPDX-FileCopyrightText: 2026 Story of Alicia contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional terms apply, see ADDITIONAL_TERMS.md.

#pragma once

#include <QObject>

class QAction;
class QMenu;
class QSystemTrayIcon;

class SystemTrayController final : public QObject
{
    Q_OBJECT

public:
    explicit SystemTrayController(QObject* parent = nullptr);
    ~SystemTrayController() override;

    [[nodiscard]] bool is_visible() const;
    void set_run_enabled(bool enabled);
    void hide();
    void retranslate();

signals:
    void open_requested();
    void run_requested();
    void quit_requested();
    void about_to_show();

private:
    QSystemTrayIcon* tray_icon {};
    QMenu* tray_menu {};
    QAction* open_action {};
    QAction* run_action {};
    QAction* quit_action {};
};
