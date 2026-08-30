#include "ui/ModalOverlay.hpp"

#include "common/GameVersion.hpp"
#include "ui/Assets.hpp"
#include "config/Config.hpp"
#include "ui/Layout.hpp"

#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QPainter>
#include <QPainterPath>

namespace util::modal_overlay
{
    namespace
    {
        QPixmap blur_pixmap(const QPixmap& source, const qreal radius)
        {
            if (source.isNull()) return {};

            QGraphicsScene scene;
            auto* item = new QGraphicsPixmapItem(source);
            auto* blur = new QGraphicsBlurEffect;
            blur->setBlurRadius(radius);
            item->setGraphicsEffect(blur);
            scene.addItem(item);

            const QRectF logical_rect = item->boundingRect();
            scene.setSceneRect(logical_rect);

            QPixmap output(source.size());
            output.setDevicePixelRatio(source.devicePixelRatio());
            output.fill(QColor(0xEA, 0xF2, 0xF7));

            QPainter painter(&output);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            scene.render(&painter, logical_rect, logical_rect, Qt::IgnoreAspectRatio);
            return output;
        }

        void draw_version_button(QPainter& painter, const QSize window_size,
                                 const QRect button_rect, const QPixmap& frame,
                                 const QPixmap& icon, const QPoint icon_offset)
        {
            const QSize icon_size = layout::scaled(icon.size(), window_size);
            painter.drawPixmap(button_rect.topLeft() + icon_offset,
                               icon.scaled(icon_size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            painter.drawPixmap(button_rect, frame);
        }
    }

    ModalOverlay::ModalOverlay(QWidget* parent)
        : QWidget(parent)
    {
        if (parent) setGeometry(parent->rect());
    }

    void ModalOverlay::show_over(QWidget* background)
    {
        if (!background) return;

        if (parentWidget() == background)
            setGeometry(background->rect());
        else
            resize(background->size());

        const QPixmap snapshot = background->grab(background->rect());
        blurred_bg = blur_pixmap(snapshot, layout::scaled(10, size()));
        show();
        raise();
        update();
    }

    void ModalOverlay::paint_frames(QPainter& painter) const
    {
        const QSize window_size = window()->size();

        const QPixmap left = assets::images[assets::Image::LeftFrame]
            .scaledToHeight(height(), Qt::SmoothTransformation);
        painter.drawPixmap(0, 0, left);

        const QPixmap right = assets::images[assets::Image::RightFrame]
            .scaledToHeight(height(), Qt::SmoothTransformation);
        painter.drawPixmap(width() - right.width(), 0, right);

        const core::game::GameVersion version = config::Config::instance().game_version();
        const QPixmap& active = assets::images[assets::Image::VersionFrameActive];
        const QPixmap& inactive = assets::images[assets::Image::VersionFrameInactive];

        draw_version_button(
            painter,
            window_size,
            layout::chrome::playtest_button(window_size),
            version == core::game::GameVersion::Playtest ? active : inactive,
            assets::images[assets::Image::VersionIconPlaytest],
            layout::chrome::playtest_icon_offset(window_size));

        draw_version_button(
            painter,
            window_size,
            layout::chrome::alicia_2_button(window_size),
            version == core::game::GameVersion::Alicia2 ? active : inactive,
            assets::images[assets::Image::VersionIconAlicia2],
            layout::chrome::alicia_2_icon_offset(window_size));
    }

    void ModalOverlay::paintEvent(QPaintEvent*)
    {
        const QSize window_size = window()->size();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        if (!blurred_bg.isNull())
        {
            const QRect background_rect = layout::region::rect(window_size);
            const int radius = layout::scaled(layout::region::k_radius, window_size);
            QPainterPath clip;
            clip.addRoundedRect(QRectF(background_rect), radius, radius);
            painter.setClipPath(clip);

            painter.drawPixmap(rect(), blurred_bg);
            painter.fillRect(rect(), QColor(255, 255, 255, 70));

            painter.setClipping(false);
        }

        if (keep_chrome) paint_frames(painter);
        paint_content(painter);
    }
}
