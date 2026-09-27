#pragma once

#include "ui/ModalOverlay.hpp"

class ImageDropdown;
class QEvent;
class QLabel;
class QPushButton;

class LanguageSelection final : public soa::ui::ModalOverlay
{
    Q_OBJECT

public:
    explicit LanguageSelection(QWidget* parent = nullptr);

signals:
    void accepted();

protected:
    void paint_content(QPainter& painter) override;
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    void setup_controls();
    void retranslate();

    QLabel* title_label {};
    QLabel* message_label {};
    QLabel* contribute_label {};
    ImageDropdown* language_dropdown {};
    QPushButton* continue_button {};
    QLabel* continue_label {};
};
