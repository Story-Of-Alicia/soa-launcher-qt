#pragma once
#include <QWidget>



namespace util::modal_overlay
{
    class ModalOverlay : public QWidget
    {
        Q_OBJECT
        public:
        explicit ModalOverlay(QWidget * parent = nullptr);

        void show_over(QWidget* background);
        [[nodiscard]] bool keeps_chrome() const { return keep_chrome; }

        signals:
            void closed();

    protected:


        virtual void paint_content(QPainter& painter) = 0;
        void set_keeps_chrome(const bool v) { keep_chrome = v; }
        void paintEvent(QPaintEvent* event) override;

    private:
        void paint_frames(QPainter& painter) const;

        QPixmap blurred_bg;
        bool    keep_chrome {true};
    };
}
