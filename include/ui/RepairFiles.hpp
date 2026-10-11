#pragma once

#include <QEvent>
#include <QString>
#include <QStringList>
#include "common/GameVersion.hpp"
#include "ui/ModalOverlay.hpp"

class QPushButton;

class RepairFiles : public soa::ui::ModalOverlay
{
    Q_OBJECT

public:
    explicit RepairFiles(QWidget* parent = nullptr);
    void refresh();
    void set_game_version(soa::common::game::GameVersion version);
    void set_detected_changes(const QStringList& paths);

signals:
    void repair_requested();
    void closed();

protected:
    void paint_content(QPainter& painter) override;
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    void setup_buttons();
    [[nodiscard]] QString detected_message() const;

    soa::common::game::GameVersion game_version {soa::common::game::GameVersion::Playtest};
    QString install_path;
    QStringList detected_changes;
    QPushButton* close_button {};
    QPushButton* cancel_button {};
    QPushButton* repair_button {};
};
