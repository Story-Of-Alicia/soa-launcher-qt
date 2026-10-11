#pragma once

#include "ui/ModalOverlay.hpp"
#include <QString>

class QTimer;
class QPushButton;

namespace soa::runtime
{
    class Shell;
}

class PrefixProgress : public soa::ui::ModalOverlay
{
    Q_OBJECT
    public:
        explicit PrefixProgress(soa::runtime::Shell* shell, QWidget* parent = nullptr);

    protected:
        void paint_content(QPainter& painter) override;
        void showEvent(QShowEvent* event) override;
        void hideEvent(QHideEvent* event) override;

        signals:
            void prefix_complete();

    private:
        void setup_buttons();

        soa::runtime::Shell* shell {};

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
