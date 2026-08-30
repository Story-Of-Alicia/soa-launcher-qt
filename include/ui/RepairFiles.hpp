#pragma once

#include <QEvent>
#include <QString>
#include "common/GameVersion.hpp"
#include "ui/ModalOverlay.hpp"

class QPushButton;

class RepairFiles : public util::modal_overlay::ModalOverlay
{
    Q_OBJECT

public:
    explicit RepairFiles(QWidget* parent = nullptr);
    void refresh();
    void set_game_version(core::game::GameVersion version);
    void set_detected_changes(const QStringList& paths);

signals:
    void repair_requested();
    void closed();

protected:
    void paint_content(QPainter& painter) override;
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    void setup_buttons();

    core::game::GameVersion game_version {core::game::GameVersion::Playtest};
    QString install_path;
    QString detected_message;
    QPushButton* close_button {};
    QPushButton* cancel_button {};
    QPushButton* repair_button {};
};
