#pragma once

#include "ui/ModalOverlay.hpp"
#include <QString>

class QTimer;
class QPushButton;

namespace core::wine
{
    class Shell;
}

class PrefixProgress : public util::modal_overlay::ModalOverlay
{
    Q_OBJECT
    public:
        explicit PrefixProgress(core::wine::Shell* shell, QWidget* parent = nullptr);

    protected:
        void paint_content(QPainter& painter) override;
        void showEvent(QShowEvent* event) override;
        void hideEvent(QHideEvent* event) override;

        signals:
            void prefix_complete();

    private:
        void setup_buttons();

        core::wine::Shell* shell {};

        QString status { "Starting..." };
        int     step   {0};
        bool    done   {};
        bool    failed {};
        bool    emitted {};

        QTimer* anim {};
        double  current_pct {0.0};
        double  target_pct  {0.0};

        QPushButton* close_button {};
};
