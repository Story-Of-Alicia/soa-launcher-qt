#include "ui/LauncherUpdate.hpp"

#include "ui/Assets.hpp"
#include "ui/Colors.hpp"
#include "i18n/LanguageManager.hpp"
#include "ui/Layout.hpp"
#include "ui/ProgressBar.hpp"
#include "ui/SimpleUtils.hpp"

#include <QComboBox>
#include <QCoreApplication>
#include <QFontMetrics>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVersionNumber>

namespace
{
    constexpr QSize k_box_size {620, 360};

    QRect box_rect(const QSize window_size)
    {
        return soa::ui::layout::centered(k_box_size, window_size, 0, 0);
    }

    QRect local_rect(const QSize window_size, const QRect source)
    {
        return soa::ui::layout::scaled(source, window_size).translated(box_rect(window_size).topLeft());
    }

    void set_translated_label(QLabel* label, const QString& source)
    {
        if (!label)
            return;
        label->setProperty("soa_i18n_text_source", source);
        label->setText(soa::i18n::translate(source));
    }

    void fit_label(QLabel* label, const int base_size, const int minimum_size)
    {
        if (!label)
            return;
        QFont font = label->font();
        const QRect available(0, 0, qMax(1, label->width()), qMax(1, label->height()));
        int size = base_size;
        while (size > minimum_size)
        {
            font.setPixelSize(size);
            const QRect bounds = QFontMetrics(font).boundingRect(
                available, Qt::AlignCenter | Qt::TextWordWrap, label->text());
            if (bounds.height() <= available.height() && bounds.width() <= available.width())
                break;
            --size;
        }
        font.setPixelSize(size);
        label->setFont(font);
    }
}

LauncherUpdate::LauncherUpdate(QWidget* parent)
    : ModalOverlay(parent)
{
    set_keeps_chrome(false);
    setup_controls();
    connect(&soa::i18n::LanguageManager::instance(),
            &soa::i18n::LanguageManager::language_changed,
            this, [this]()
    {
        retranslate_content();
        update();
    });
}

void LauncherUpdate::setup_controls()
{
    const QSize w = window()->size();

    title_label = new QLabel(this);
    title_label->setGeometry(local_rect(w, {42, 26, 536, 40}));
    title_label->setAlignment(Qt::AlignCenter);
    title_label->setWordWrap(true);
    QFont title_font = soa::ui::assets::fonts[soa::ui::assets::Font::EurostileExtraBlack];
    title_font.setPixelSize(soa::ui::layout::scaled(25, w));
    title_font.setWeight(QFont::Black);
    title_label->setFont(title_font);
    title_label->setStyleSheet(QStringLiteral("color:#4F1717; background:transparent;"));

    message_label = new QLabel(this);
    message_label->setGeometry(local_rect(w, {70, 78, 480, 56}));
    message_label->setAlignment(Qt::AlignCenter);
    message_label->setWordWrap(true);
    QFont message_font = soa::ui::assets::fonts[soa::ui::assets::Font::Inter];
    message_font.setPixelSize(soa::ui::layout::scaled(16, w));
    message_font.setWeight(QFont::Medium);
    message_label->setFont(message_font);
    message_label->setStyleSheet(QStringLiteral("color:#392518; background:transparent;"));

    details_label = new QLabel(this);
    details_label->setGeometry(local_rect(w, {70, 136, 480, 46}));
    details_label->setAlignment(Qt::AlignCenter);
    details_label->setWordWrap(true);
    QFont details_font = soa::ui::assets::fonts[soa::ui::assets::Font::Inter];
    details_font.setPixelSize(soa::ui::layout::scaled(14, w));
    details_font.setWeight(QFont::Medium);
    details_label->setFont(details_font);
    details_label->setStyleSheet(QStringLiteral("color:#A08C7B; background:transparent;"));

    version_combo = new QComboBox(this);
    version_combo->setGeometry(local_rect(w, {100, 186, 420, 42}));
    version_combo->setCursor(Qt::PointingHandCursor);
    version_combo->setMaxVisibleItems(3);
    version_combo->setStyleSheet(QStringLiteral(
        "QComboBox { background:#F7F0EB; color:#4F1717; border:1px solid #A98678; "
        "border-radius:%1px; padding:%2px %3px; font-size:%4px; }"
        "QComboBox::drop-down { border:0; width:%5px; }"
        "QComboBox QAbstractItemView { background:#F7F0EB; color:#4F1717; "
        "selection-background-color:#EBDCD3; selection-color:#4F1717; "
        "border:1px solid #A98678; outline:0; font-size:%4px; }"
        "QComboBox QAbstractItemView::item { min-height:%6px; padding:%7px %8px; }")
        .arg(soa::ui::layout::scaled(6, w))
        .arg(soa::ui::layout::scaled(5, w))
        .arg(soa::ui::layout::scaled(12, w))
        .arg(qMax(9, soa::ui::layout::scaled(13, w)))
        .arg(soa::ui::layout::scaled(28, w))
        .arg(soa::ui::layout::scaled(34, w))
        .arg(soa::ui::layout::scaled(3, w))
        .arg(soa::ui::layout::scaled(10, w)));
    version_combo->hide();
    connect(version_combo, &QComboBox::currentTextChanged, this,
            [this](const QString& version)
    {
        if (!catalogue_mode || version.isEmpty())
            return;
        release_version = version;
        emit version_selected(version);
        retranslate_content();
    });

    progress_label = new QLabel(this);
    progress_label->setGeometry(local_rect(w, {74, 218, 472, 22}));
    progress_label->setAlignment(Qt::AlignCenter);
    QFont progress_font = soa::ui::assets::fonts[soa::ui::assets::Font::Inter];
    progress_font.setPixelSize(soa::ui::layout::scaled(12, w));
    progress_font.setWeight(QFont::DemiBold);
    progress_label->setFont(progress_font);
    progress_label->setStyleSheet(QStringLiteral("color:#4F1717; background:transparent;"));

    update_button = soa::ui::simple_utils::make_flat_button(this);
    update_button->setGeometry(local_rect(w, {318, 254, 216, 44}));
    update_button->setIconSize(update_button->size());
    update_button->setProperty("soa_button_stretch_asset", true);
    update_button->installEventFilter(this);
    update_button->setAccessibleName(soa::i18n::translate("Update launcher now"));

    update_button_label = new QLabel(update_button);
    update_button_label->setGeometry(update_button->rect());
    update_button_label->setAlignment(Qt::AlignCenter);
    update_button_label->setAttribute(Qt::WA_TransparentForMouseEvents);
    QFont button_font = soa::ui::assets::fonts[soa::ui::assets::Font::EurostileExtraBlack];
    button_font.setPixelSize(soa::ui::layout::scaled(21, w));
    button_font.setWeight(QFont::Black);
    update_button_label->setFont(button_font);
    update_button_label->setStyleSheet(QStringLiteral(
        "QLabel { color:#FFFFFF; background:transparent; }"
        "QLabel:disabled { color:#FFFFFF; }"));
    update_button_label->raise();

    cancel_button = soa::ui::simple_utils::make_flat_button(this);
    cancel_button->setGeometry(local_rect(w, {86, 254, 216, 44}));
    cancel_button->setIconSize(cancel_button->size());
    cancel_button->setProperty("soa_button_stretch_asset", true);
    cancel_button->installEventFilter(this);
    QFont cancel_font = soa::ui::assets::fonts[soa::ui::assets::Font::EurostileExtraBlack];
    cancel_font.setPixelSize(soa::ui::layout::scaled(14, w));
    cancel_font.setWeight(QFont::Black);
    soa::ui::simple_utils::add_button_text(
        cancel_button, soa::ui::assets::Button::Cancel,
        QStringLiteral("CANCEL"), cancel_font);

    close_button = soa::ui::simple_utils::make_flat_button(this);
    close_button->setGeometry(
        local_rect(w, soa::ui::layout::modal_close::rect_in(
                          {0, 0, k_box_size.width(), k_box_size.height()})));
    close_button->setIcon(QIcon(soa::ui::assets::images[soa::ui::assets::Image::CloseSettings]));
    close_button->setIconSize(soa::ui::layout::scaled(soa::ui::layout::modal_close::k_icon, w));
    close_button->setAccessibleName(soa::i18n::translate("Postpone launcher update"));

    connect(update_button, &QPushButton::clicked, this, [this]()
    {
        if (!downloading_update && !starting_installer)
            emit update_requested();
    });
    connect(cancel_button, &QPushButton::clicked, this, [this]()
    {
        if (starting_installer || required_update)
            return;
        emit postponed();
    });
    connect(close_button, &QPushButton::clicked, this, [this]()
    {
        if (!required_update && !downloading_update && !starting_installer)
            emit postponed();
    });

    refresh_layout();
    retranslate_content();
}

void LauncherUpdate::set_versions(const QString& installed_version,
                                  const QStringList& versions,
                                  const bool catalogue_visible)
{
    current_version = installed_version;
    catalogue_mode = catalogue_visible && !versions.isEmpty();
    version_combo->blockSignals(true);
    version_combo->clear();
    version_combo->addItems(versions);
    if (version_combo->count() > 0)
    {
        version_combo->setCurrentIndex(0);
        release_version = version_combo->currentText().trimmed();
    }
    else
    {
        release_version.clear();
    }
    version_combo->blockSignals(false);
    refresh_layout();
    retranslate_content();
}

void LauncherUpdate::set_release(const QString& version, const bool required,
                                 const QString& message)
{
    const QString requested_version = version.trimmed();
    if (version_combo && version_combo->count() > 0)
    {
        const int requested_index = version_combo->findText(requested_version);
        if (requested_index >= 0)
        {
            version_combo->blockSignals(true);
            version_combo->setCurrentIndex(requested_index);
            version_combo->blockSignals(false);
        }
        release_version = version_combo->currentText().trimmed();
    }
    else
    {
        release_version = requested_version;
    }
    required_update = required;
    release_message = message.trimmed();
    downloading_update = false;
    starting_installer = false;
    refresh_layout();
    retranslate_content();
}

void LauncherUpdate::set_downloading(const bool downloading)
{
    downloading_update = downloading;
    starting_installer = false;
    if (downloading_update)
    {
        progress_fraction = 0.0;
        set_translated_label(progress_label, QStringLiteral("Preparing download..."));
    }
    refresh_layout();
    retranslate_content();
}

void LauncherUpdate::set_progress(const qint64 received, const qint64 total)
{
    if (!downloading_update)
        return;
    if (total > 0)
    {
        progress_fraction = qBound(0.0,
                                   static_cast<double>(received) / static_cast<double>(total),
                                   1.0);
        const double received_mb = static_cast<double>(received) / (1024.0 * 1024.0);
        const double total_mb = static_cast<double>(total) / (1024.0 * 1024.0);
        set_translated_label(
            progress_label,
            QStringLiteral("%1 MB of %2 MB")
                .arg(QString::number(received_mb, 'f', 1),
                     QString::number(total_mb, 'f', 1)));
    }
    else
    {
        progress_fraction = 0.0;
        const double received_mb = static_cast<double>(received) / (1024.0 * 1024.0);
        set_translated_label(
            progress_label,
            QStringLiteral("%1 MB downloaded")
                .arg(QString::number(received_mb, 'f', 1)));
    }
    update();
}

void LauncherUpdate::set_starting_installer()
{
    downloading_update = false;
    starting_installer = true;
    progress_fraction = 1.0;
    set_translated_label(progress_label, QStringLiteral("Starting installer..."));
    refresh_layout();
    retranslate_content();
}

void LauncherUpdate::refresh_layout()
{
    const QSize w = window()->size();
    const bool progress_visible = downloading_update || starting_installer;
    details_label->setGeometry(local_rect(w, catalogue_mode
        ? QRect{70, 136, 480, 42}
        : QRect{70, 136, 480, 46}));
    progress_label->setVisible(progress_visible);
    version_combo->setVisible(catalogue_mode && !progress_visible);
    const bool cancel_visible = !required_update && !starting_installer;
    const int button_y = catalogue_mode ? 258 : 228;
    constexpr int button_width = 216;
    constexpr int button_height = 44;
    constexpr int button_gap = 16;
    constexpr int pair_left = 86;
    constexpr int centered_left = 202;
    cancel_button->setGeometry(local_rect(w, downloading_update
        ? QRect{centered_left, 264, button_width, button_height}
        : QRect{pair_left, button_y, button_width, button_height}));
    cancel_button->setIconSize(cancel_button->size());
    cancel_button->setVisible(cancel_visible);
    cancel_button->setEnabled(cancel_visible);
    update_button->setGeometry(local_rect(w, cancel_visible
        ? QRect{pair_left + button_width + button_gap, button_y, button_width, button_height}
        : QRect{centered_left, button_y, button_width, button_height}));
    update_button->setIconSize(update_button->size());
    update_button_label->setGeometry(update_button->rect());
    update_button->setVisible(!progress_visible);
    update_button->setEnabled(!progress_visible);
    update_button_label->setVisible(!progress_visible);
    update_button_label->setEnabled(true);
    close_button->setVisible(!required_update && !progress_visible);
    close_button->setEnabled(!progress_visible);
    set_button_pixmap(soa::ui::assets::translated_buttons[soa::ui::assets::Button::UpdateAvailable].normal);
    soa::ui::simple_utils::refresh_button(cancel_button);
    update_button_label->raise();
    cancel_button->raise();
    update();
}

void LauncherUpdate::retranslate_content()
{
    QString shown_version;
    if (version_combo && version_combo->count() > 0)
        shown_version = version_combo->currentText().trimmed();
    if (shown_version.isEmpty())
        shown_version = release_version.trimmed();
    if (shown_version.isEmpty())
        shown_version = current_version.trimmed();
    if (shown_version.isEmpty())
        shown_version = QCoreApplication::applicationVersion().trimmed();

    QString title_source;
    QString message_source;
    QString details_source;

    if (starting_installer)
    {
        title_source = QStringLiteral("STARTING LAUNCHER UPDATE");
        message_source = QStringLiteral(
            "The installer is ready. The launcher will close automatically.");
        details_source = QStringLiteral(
            "Complete the installer, then open Story of Alicia again.");
        set_update_button_text(QStringLiteral("STARTING..."));
    }
    else if (downloading_update)
    {
        title_source = QStringLiteral("DOWNLOADING LAUNCHER UPDATE");
        message_source = QStringLiteral("Downloading version %1...").arg(shown_version);
        details_source = QStringLiteral("The update is verified before it is installed.");
        set_update_button_text(QStringLiteral("DOWNLOADING..."));
    }
    else
    {
        title_source = required_update
            ? QStringLiteral("LAUNCHER UPDATE REQUIRED")
            : QStringLiteral("LAUNCHER UPDATES");
        if (catalogue_mode)
        {
            message_source = QStringLiteral("Choose from up to three signed launcher releases.");
        }
        else if (required_update)
        {
            message_source = QStringLiteral(
                "Version %1 is available. You must update the launcher before continuing.")
                .arg(shown_version);
        }
        else
        {
            message_source = QStringLiteral("Version %1 is available for the launcher.")
                .arg(shown_version);
        }
#if defined(Q_OS_MACOS)
        const QString default_details = QStringLiteral(
            "The installer will open automatically. The launcher will close.");
#else
        const QString default_details = QStringLiteral(
            "The AppImage will update and restart automatically.");
#endif
        details_source = catalogue_mode
            ? QStringLiteral("Installed: %1 · Selected: %2").arg(current_version, shown_version)
            : release_message.isEmpty() ? default_details : release_message;
        if (!catalogue_mode)
            set_update_button_text(QStringLiteral("UPDATE NOW"));
        else if (shown_version == current_version)
            set_update_button_text(QStringLiteral("REINSTALL VERSION"));
        else if (QVersionNumber::compare(QVersionNumber::fromString(shown_version),
                                         QVersionNumber::fromString(current_version)) < 0)
            set_update_button_text(QStringLiteral("DOWNGRADE"));
        else
            set_update_button_text(QStringLiteral("UPDATE NOW"));
    }

    set_translated_label(title_label, title_source);
    set_translated_label(message_label, message_source);
    set_translated_label(details_label, details_source);

    fit_label(title_label, soa::ui::layout::scaled(25, window()->size()), qMax(12, soa::ui::layout::scaled(18, window()->size())));
    fit_label(message_label, soa::ui::layout::scaled(16, window()->size()), qMax(9, soa::ui::layout::scaled(12, window()->size())));
    fit_label(details_label, soa::ui::layout::scaled(14, window()->size()), qMax(8, soa::ui::layout::scaled(11, window()->size())));
    update_button->setAccessibleName(soa::i18n::translate("Update launcher now"));
    soa::ui::simple_utils::set_button_text(cancel_button, QStringLiteral("CANCEL"));
    cancel_button->setAccessibleName(soa::i18n::translate(
        downloading_update ? "Cancel launcher update download" : "Cancel launcher update"));
    close_button->setAccessibleName(soa::i18n::translate("Postpone launcher update"));
}

void LauncherUpdate::set_update_button_text(const QString& source)
{
    update_button_source = source;
    set_translated_label(update_button_label, source);
    fit_label(update_button_label, soa::ui::layout::scaled(21, window()->size()), qMax(10, soa::ui::layout::scaled(14, window()->size())));
}

void LauncherUpdate::set_button_pixmap(const QPixmap& pixmap)
{
    const QPixmap scaled = pixmap.scaled(update_button->iconSize(), Qt::IgnoreAspectRatio,
                                         Qt::SmoothTransformation);
    QIcon icon;
    icon.addPixmap(scaled, QIcon::Normal);
    icon.addPixmap(scaled, QIcon::Disabled);
    update_button->setIcon(icon);
}

void LauncherUpdate::paint_content(QPainter& painter)
{
    painter.drawPixmap(box_rect(window()->size()),
                       soa::ui::assets::images[soa::ui::assets::Image::BoxUpdate]);
    if (downloading_update || starting_installer)
    {
        soa::ui::progress_bar::draw(
            painter,
            local_rect(window()->size(), {74, 194, 472, 21}),
            progress_fraction);
    }
}

bool LauncherUpdate::eventFilter(QObject* object, QEvent* event)
{
    if (object == cancel_button && cancel_button->isEnabled())
    {
        const auto& asset = soa::ui::assets::button(soa::ui::assets::Button::Cancel);
        soa::ui::simple_utils::apply_button_state(
            event, cancel_button, asset.normal, asset.hover, asset.clicked);
    }
    else if (object == update_button && update_button->isEnabled())
    {
        const auto& assets = soa::ui::assets::translated_buttons[soa::ui::assets::Button::UpdateAvailable];
        switch (event->type())
        {
            case QEvent::Enter:
                set_button_pixmap(assets.hover);
                break;
            case QEvent::Leave:
                set_button_pixmap(assets.normal);
                break;
            case QEvent::MouseButtonPress:
                set_button_pixmap(assets.clicked);
                break;
            case QEvent::MouseButtonRelease:
                set_button_pixmap(update_button->underMouse() ? assets.hover : assets.normal);
                break;
            default:
                break;
        }
    }
    return QWidget::eventFilter(object, event);
}
