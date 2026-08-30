#pragma once

#include <QPushButton>
#include "ui/ModalOverlay.hpp"

namespace core::wine
{
    class Shell;
}
class DownloadProgress;

class GameInstall : public util::modal_overlay::ModalOverlay
{
    Q_OBJECT
    public:
        explicit GameInstall(core::wine::Shell* shell, QWidget* parent = nullptr);
        void refresh_game_path();
        signals:
            void closed();
    protected:
        void paint_content(QPainter& painter) override;
        bool eventFilter(QObject* obj, QEvent* event) override;
    private:
        void setup_close_button();
        void setup_buttons();
        void start_install();
        void set_installing(bool value);
        bool path_inside_prefix() const;
        QString game_path {};
        bool    show_warning {};
        QPushButton* close_button {};
        QPushButton* install_button {};
        QPushButton* cancel_button {};
        QPushButton* change_path_button {};
        core::wine::Shell * shell {};
        bool installing {};
        DownloadProgress * download {};
};
