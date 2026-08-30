#include "ui/SimpleUtils.hpp"

#include "i18n/LanguageManager.hpp"
#include "ui/Layout.hpp"
#include "ui/Styles.hpp"

#include <QColor>
#include <QEvent>
#include <QFontMetrics>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QPalette>
#include <QPixmap>
#include <QVariant>

#include <initializer_list>

namespace util::simple_utils
{
    void make_label_block(QWidget* parent, const QSize window_size, const int y,
                          const QString& title, const QString& description)
    {
        auto* title_label = new QLabel(i18n::translate(title), parent);
        QFont title_font = assets::fonts[assets::Font::EurostileBlack];
        title_font.setPixelSize(layout::scaled(layout::text::k_row_title, window_size));
        title_font.setWeight(QFont::Black);
        title_label->setFont(title_font);
        title_label->setStyleSheet(QStringLiteral("color: #4F1717; background: transparent;"));
        title_label->setGeometry(layout::settings::row_title(window_size, y));
        title_label->setProperty("soa_i18n_text_source", title);

        auto* description_label = new QLabel(i18n::translate(description), parent);
        description_label->setWordWrap(true);
        QFont description_font = assets::fonts[assets::Font::Inter];
        description_font.setPixelSize(layout::scaled(layout::text::k_desc, window_size));
        description_font.setWeight(QFont::Medium);
        description_label->setFont(description_font);
        description_label->setStyleSheet(QStringLiteral("color: #4F1717; background: transparent;"));
        description_label->setGeometry(layout::settings::row_desc(window_size, y));
        description_label->setProperty("soa_i18n_text_source", description);

        const int title_base_size = title_font.pixelSize();
        const int description_base_size = description_font.pixelSize();
        const auto refit = [title_label, description_label, window_size,
                            title_base_size, description_base_size]()
        {
            QFont fitted_title = title_label->font();
            const int title_min = qMax(9, layout::scaled(12, window_size));
            int title_size = qMax(title_min, title_base_size);
            while (title_size > title_min)
            {
                fitted_title.setPixelSize(title_size);
                if (QFontMetrics(fitted_title).horizontalAdvance(title_label->text())
                    <= qMax(1, title_label->width() - layout::scaled(4, window_size)))
                {
                    break;
                }
                --title_size;
            }
            fitted_title.setPixelSize(title_size);
            title_label->setFont(fitted_title);
            title_label->setToolTip(title_label->text());

            QFont fitted_description = description_label->font();
            const int description_min = qMax(8, layout::scaled(10, window_size));
            int description_size = qMax(description_min, description_base_size);
            const QRect available(0, 0, qMax(1, description_label->width()),
                                  qMax(1, description_label->height()));
            while (description_size > description_min)
            {
                fitted_description.setPixelSize(description_size);
                const QRect bounds = QFontMetrics(fitted_description).boundingRect(
                    available, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop,
                    description_label->text());
                if (bounds.height() <= available.height())
                    break;
                --description_size;
            }
            fitted_description.setPixelSize(description_size);
            description_label->setFont(fitted_description);
            description_label->setToolTip(description_label->text());
        };

        refit();
        QObject::connect(&i18n::LanguageManager::instance(),
                         &i18n::LanguageManager::language_changed,
                         title_label, [refit]() { refit(); });
    }

    QPushButton* make_flat_button(QWidget* parent)
    {
        auto* button = new QPushButton(parent);
        button->setFlat(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(styles::k_flat_transparent);
        return button;
    }

    namespace
    {
        constexpr auto k_button_text_name = "soa_button_text_overlay";
        constexpr auto k_text_source_property = "soa_i18n_text_source";
        constexpr auto k_base_pixel_size_property = "soa_button_text_base_pixel_size";
        constexpr auto k_asset_property = "soa_button_asset";
        constexpr auto k_loading_property = "soa_button_loading";
        constexpr auto k_connected_property = "soa_button_language_connected";
        constexpr auto k_stretch_asset_property = "soa_button_stretch_asset";

        QLabel* button_text_label(QPushButton* button)
        {
            return button
                ? button->findChild<QLabel*>(QString::fromLatin1(k_button_text_name),
                                             Qt::FindDirectChildrenOnly)
                : nullptr;
        }

        void enforce_white_button_text(QLabel* label)
        {
            if (!label)
                return;

            constexpr int k_disabled_text_alpha = 140;
            QPalette palette = label->palette();
            for (const QPalette::ColorGroup group :
                 {QPalette::Active, QPalette::Inactive})
            {
                palette.setColor(group, QPalette::WindowText, Qt::white);
                palette.setColor(group, QPalette::Text, Qt::white);
            }
            const QColor faded(255, 255, 255, k_disabled_text_alpha);
            palette.setColor(QPalette::Disabled, QPalette::WindowText, faded);
            palette.setColor(QPalette::Disabled, QPalette::Text, faded);
            label->setPalette(palette);
            label->setStyleSheet(QStringLiteral(
                "QLabel { background: transparent; color: #FFFFFF; }"
                "QLabel:disabled { color: rgba(255,255,255,140); }"));
        }

        void fit_button_text(QLabel* label)
        {
            if (!label)
                return;

            QFont font = label->font();
            const int base_size = label->property(k_base_pixel_size_property).toInt();
            const int maximum_width = qMax(1, label->width() - layout::scaled(12, label->window()->size()));
            const int minimum_pixel_size = qMax(8, layout::scaled(9, label->window()->size()));
            int pixel_size = qMax(minimum_pixel_size, base_size);
            while (pixel_size > minimum_pixel_size)
            {
                font.setPixelSize(pixel_size);
                if (QFontMetrics(font).horizontalAdvance(label->text()) <= maximum_width)
                    break;
                --pixel_size;
            }
            font.setPixelSize(pixel_size);
            label->setFont(font);
        }

        assets::Button button_asset_key(QPushButton* button)
        {
            return static_cast<assets::Button>(button->property(k_asset_property).toInt());
        }

        QPixmap displayed_button_pixmap(QPushButton* button, const QPixmap& source)
        {
            if (!button || source.isNull()
                || !button->property(k_stretch_asset_property).toBool())
            {
                return source;
            }

            const QSize target = button->iconSize().isValid()
                ? button->iconSize()
                : button->size();
            return source.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }





        QPixmap disabled_button_pixmap(const QPixmap& source)
        {
            if (source.isNull())
                return source;

            static QHash<qint64, QPixmap> cache;
            const qint64 key = source.cacheKey();
            if (const auto found = cache.constFind(key); found != cache.constEnd())
                return found.value();

            QImage image = source.toImage().convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < image.height(); ++y)
            {
                auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
                for (int x = 0; x < image.width(); ++x)
                {
                    const QRgb pixel = line[x];
                    const int alpha = qAlpha(pixel);
                    if (alpha == 0)
                        continue;
                    const int grey = qGray(pixel);
                    line[x] = qRgba((qRed(pixel) + grey * 3) / 4,
                                    (qGreen(pixel) + grey * 3) / 4,
                                    (qBlue(pixel) + grey * 3) / 4,
                                    alpha * 110 / 255);
                }
            }

            QPixmap result = QPixmap::fromImage(image);
            result.setDevicePixelRatio(source.devicePixelRatio());
            if (cache.size() > 64)
                cache.clear();
            cache.insert(key, result);
            return result;
        }

        void set_button_icon(QPushButton* button, const QPixmap& source)
        {
            const QPixmap displayed = displayed_button_pixmap(button, source);
            QIcon icon;
            icon.addPixmap(displayed, QIcon::Normal);
            icon.addPixmap(disabled_button_pixmap(displayed), QIcon::Disabled);
            button->setIcon(icon);
        }
    }

    void refresh_button(QPushButton* button)
    {
        if (!button || !button->property(k_asset_property).isValid())
            return;

        const assets::Button key = button_asset_key(button);
        const assets::ButtonAsset& asset = assets::button(key);
        const bool loading = button->property(k_loading_property).toBool();
        const QPixmap& pixmap = loading && !asset.loading.isNull() ? asset.loading : asset.normal;
        set_button_icon(button, pixmap);

        if (QLabel* label = button_text_label(button))
        {
            const QString source = label->property(k_text_source_property).toString();
            label->setText(i18n::translate(source));
            enforce_white_button_text(label);
            fit_button_text(label);
            label->setVisible(assets::translated_button_assets_active());
            label->raise();
        }
    }

    void set_button_asset(QPushButton* button, const assets::Button asset)
    {
        if (!button)
            return;
        button->setProperty(k_asset_property, static_cast<int>(asset));
        refresh_button(button);
    }

    void set_button_enabled(QPushButton* button, const bool enabled)
    {
        if (!button)
            return;
        button->setEnabled(enabled);
        button->setCursor(enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }

    void set_button_loading(QPushButton* button, const bool loading)
    {
        if (!button)
            return;
        button->setProperty(k_loading_property, loading);
        refresh_button(button);
    }

    QLabel* add_button_text(QPushButton* button, const assets::Button asset,
                            const QString& source, const QFont& font,
                            const QColor&, const QPoint offset)
    {
        if (!button)
            return nullptr;

        QLabel* label = button_text_label(button);
        if (!label)
        {
            label = new QLabel(button);
            label->setObjectName(QString::fromLatin1(k_button_text_name));
            label->setAttribute(Qt::WA_TransparentForMouseEvents);
            label->setAlignment(Qt::AlignCenter);
            label->setTextFormat(Qt::PlainText);
        }

        label->setGeometry(button->rect().adjusted(offset.x(), offset.y(), 0, 0));
        label->setFont(font);
        label->setProperty(k_base_pixel_size_property, font.pixelSize());
        label->setProperty(k_text_source_property, source);
        enforce_white_button_text(label);
        button->setProperty(k_asset_property, static_cast<int>(asset));
        button->setProperty(k_loading_property, false);

        if (!button->property(k_connected_property).toBool())
        {
            button->setProperty(k_connected_property, true);
            QObject::connect(&i18n::LanguageManager::instance(),
                             &i18n::LanguageManager::language_changed,
                             button, [button]()
            {
                refresh_button(button);
            });
        }

        refresh_button(button);
        return label;
    }

    void set_button_text(QPushButton* button, const QString& source)
    {
        QLabel* label = button_text_label(button);
        if (!label)
            return;
        label->setProperty(k_text_source_property, source);
        refresh_button(button);
    }

    bool apply_button_state(QEvent* event, QPushButton* button,
                            const QPixmap& normal, const QPixmap& hover,
                            const QPixmap& clicked)
    {
        switch (event->type())
        {
            case QEvent::Enter:
                set_button_icon(button, hover);
                return true;
            case QEvent::Leave:
                set_button_icon(button, normal);
                return true;
            case QEvent::MouseButtonPress:
                set_button_icon(button, clicked);
                return true;
            case QEvent::MouseButtonRelease:
                set_button_icon(button, button->underMouse() ? hover : normal);
                return true;
            default:
                return false;
        }
    }
}
