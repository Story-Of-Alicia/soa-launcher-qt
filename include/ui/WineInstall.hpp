#pragma once
#include <QPushButton>
#include "ui/PrefixProgress.hpp"
#include "ui/ModalOverlay.hpp"

namespace soa::runtime
{
    class Shell;
}
class PrefixProgress;

class WineInstall : public soa::ui::ModalOverlay
{
    Q_OBJECT
    public:
        explicit WineInstall(soa::runtime::Shell* shell, QWidget* parent = nullptr);
        void refresh_prefix_path();
    protected:
        void paint_content(QPainter& painter) override;
        bool eventFilter(QObject* obj, QEvent* event) override;
        signals:
            void closed();
    private:
        void setup_close_button();
        void setup_buttons();
        QString game_path {};
        QString warn_message {};
        QPushButton* close_button {};
        QPushButton* install_button {};
        QPushButton* cancel_button {};
        QPushButton* change_path_button {};
        soa::runtime::Shell * shell {};
        PrefixProgress * prefix_progress {};
        bool        installing {};
        void start_install();
        void set_installing(bool value);
};
