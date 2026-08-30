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
        return util::layout::centered(k_box_size, window_size, 0, 0);
    }

    QRect local_rect(const QSize window_size, const QRect source)
    {
        return util::layout::scaled(source, window_size).translated(box_rect(window_size).topLeft());
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
    connect(&util::i18n::LanguageManager::instance(),
            &util::i18n::LanguageManager::language_changed,
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
    QFont title_font = util::assets::fonts[util::assets::Font::EurostileExtraBlack];
    title_font.setPixelSize(util::layout::scaled(25, w));
    title_font.setWeight(QFont::Black);
    title_label->setFont(title_font);
    title_label->setStyleSheet(QStringLiteral("color:#4F1717; background:transparent;"));

    message_label = new QLabel(this);
    message_label->setGeometry(local_rect(w, {70, 78, 480, 56}));
    message_label->setAlignment(Qt::AlignCenter);
    message_label->setWordWrap(true);
    QFont message_font = util::assets::fonts[util::assets::Font::Inter];
    message_font.setPixelSize(util::layout::scaled(16, w));
    message_font.setWeight(QFont::Medium);
    message_label->setFont(message_font);
    message_label->setStyleSheet(QStringLiteral("color:#392518; background:transparent;"));

    details_label = new QLabel(this);
    details_label->setGeometry(local_rect(w, {70, 136, 480, 46}));
    details_label->setAlignment(Qt::AlignCenter);
    details_label->setWordWrap(true);
    QFont details_font = util::assets::fonts[util::assets::Font::Inter];
    details_font.setPixelSize(util::layout::scaled(14, w));
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
        .arg(util::layout::scaled(6, w))
        .arg(util::layout::scaled(5, w))
        .arg(util::layout::scaled(12, w))
        .arg(qMax(9, util::layout::scaled(13, w)))
        .arg(util::layout::scaled(28, w))
        .arg(util::layout::scaled(34, w))
        .arg(util::layout::scaled(3, w))
        .arg(util::layout::scaled(10, w)));
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
    QFont progress_font = util::assets::fonts[util::assets::Font::Inter];
    progress_font.setPixelSize(util::layout::scaled(12, w));
    progress_font.setWeight(QFont::DemiBold);
    progress_label->setFont(progress_font);
    progress_label->setStyleSheet(QStringLiteral("color:#4F1717; background:transparent;"));

    update_button = util::simple_utils::make_flat_button(this);
    update_button->setGeometry(local_rect(w, {242, 254, 304, 56}));
    update_button->setIconSize(update_button->size());
    update_button->setProperty("soa_button_stretch_asset", true);
    update_button->installEventFilter(this);
    update_button->setAccessibleName(QStringLiteral("Update launcher now"));

    update_button_label = new QLabel(update_button);
    update_button_label->setGeometry(update_button->rect());
    update_button_label->setAlignment(Qt::AlignCenter);
    update_button_label->setAttribute(Qt::WA_TransparentForMouseEvents);
    QFont button_font = util::assets::fonts[util::assets::Font::EurostileExtraBlack];
    button_font.setPixelSize(util::layout::scaled(21, w));
    button_font.setWeight(QFont::Black);
    update_button_label->setFont(button_font);
    update_button_label->setStyleSheet(QStringLiteral(
        "QLabel { color:#FFFFFF; background:transparent; }"
        "QLabel:disabled { color:#FFFFFF; }"));
    update_button_label->raise();

    cancel_button = util::simple_utils::make_flat_button(this);
    cancel_button->setGeometry(local_rect(w, {86, 260, 174, 36}));
    cancel_button->setIconSize(cancel_button->size());
    cancel_button->setProperty("soa_button_stretch_asset", true);
    cancel_button->installEventFilter(this);
    QFont cancel_font = util::assets::fonts[util::assets::Font::EurostileExtraBlack];
    cancel_font.setPixelSize(util::layout::scaled(12, w));
    cancel_font.setWeight(QFont::Black);
    util::simple_utils::add_button_text(
        cancel_button, util::assets::Button::Cancel,
        QStringLiteral("CANCEL"), cancel_font);

    close_button = util::simple_utils::make_flat_button(this);
    close_button->setGeometry(
        local_rect(w, util::layout::modal_close::rect_in(
                          {0, 0, k_box_size.width(), k_box_size.height()})));
    close_button->setIcon(QIcon(util::assets::images[util::assets::Image::CloseSettings]));
    close_button->setIconSize(util::layout::scaled(util::layout::modal_close::k_icon, w));
    close_button->setAccessibleName(QStringLiteral("Postpone launcher update"));

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
    const int selected = version_combo->findText(release_version);
    if (selected >= 0)
        version_combo->setCurrentIndex(selected);
    else if (version_combo->count() > 0)
    {
        version_combo->setCurrentIndex(0);
        release_version = version_combo->currentText();
    }
    version_combo->blockSignals(false);
    refresh_layout();
    retranslate_content();
}

void LauncherUpdate::set_release(const QString& version, const bool required,
                                 const QString& message)
{
    release_version = version.trimmed();
    if (release_version.isEmpty() && version_combo && version_combo->count() > 0)
        release_version = version_combo->currentText().trimmed();
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
        progress_label->setText(util::i18n::translate("Preparing download..."));
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
        progress_label->setText(util::i18n::translate("%1 MB of %2 MB")
                                    .arg(QString::number(received_mb, 'f', 1),
                                         QString::number(total_mb, 'f', 1)));
    }
    else
    {
        progress_fraction = 0.0;
        const double received_mb = static_cast<double>(received) / (1024.0 * 1024.0);
        progress_label->setText(util::i18n::translate("%1 MB downloaded")
                                    .arg(QString::number(received_mb, 'f', 1)));
    }
    update();
}

void LauncherUpdate::set_starting_installer()
{
    downloading_update = false;
    starting_installer = true;
    progress_fraction = 1.0;
    progress_label->setText(util::i18n::translate("Starting installer..."));
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
    cancel_button->setGeometry(local_rect(w, downloading_update
        ? QRect{223, 264, 174, 36}
        : QRect{86, button_y + 4, 174, 36}));
    cancel_button->setIconSize(cancel_button->size());
    cancel_button->setVisible(cancel_visible);
    cancel_button->setEnabled(cancel_visible);
    update_button->setGeometry(local_rect(w, cancel_visible
        ? QRect{276, button_y, 258, 44}
        : QRect{181, button_y, 258, 44}));
    update_button->setIconSize(update_button->size());
    update_button_label->setGeometry(update_button->rect());
    update_button->setVisible(!progress_visible);
    update_button->setEnabled(!progress_visible);
    update_button_label->setVisible(!progress_visible);
    update_button_label->setEnabled(true);
    close_button->setVisible(!required_update && !progress_visible);
    close_button->setEnabled(!progress_visible);
    set_button_pixmap(util::assets::translated_buttons[util::assets::Button::UpdateAvailable].normal);
    util::simple_utils::refresh_button(cancel_button);
    update_button_label->raise();
    cancel_button->raise();
    update();
}

void LauncherUpdate::retranslate_content()
{
    QString shown_version = release_version.trimmed();
    if (shown_version.isEmpty() && version_combo)
        shown_version = version_combo->currentText().trimmed();
    if (shown_version.isEmpty())
        shown_version = current_version.trimmed();
    if (shown_version.isEmpty())
        shown_version = QCoreApplication::applicationVersion().trimmed();
    if (starting_installer)
    {
        title_label->setText(util::i18n::translate("STARTING LAUNCHER UPDATE"));
        message_label->setText(util::i18n::translate(
            "The installer is ready. The launcher will close automatically."));
        details_label->setText(util::i18n::translate(
            "Complete the installer, then open Story of Alicia again."));
        set_update_button_text(QStringLiteral("STARTING..."));
    }
    else if (downloading_update)
    {
        title_label->setText(util::i18n::translate("DOWNLOADING LAUNCHER UPDATE"));
        message_label->setText(util::i18n::translate("Downloading version %1...")
                                   .arg(shown_version));
        details_label->setText(util::i18n::translate(
            "The update is verified before it is installed."));
        set_update_button_text(QStringLiteral("DOWNLOADING..."));
    }
    else
    {
        title_label->setText(util::i18n::translate(catalogue_mode
            ? "LAUNCHER VERSIONS"
            : required_update ? "LAUNCHER UPDATE REQUIRED"
                              : "LAUNCHER UPDATE AVAILABLE"));
        if (catalogue_mode)
        {
            message_label->setText(util::i18n::translate(
                "Choose from up to three signed launcher releases."));
        }
        else if (required_update)
        {
            message_label->setText(util::i18n::translate(
                "Version %1 is available. You must update the launcher before continuing.",
                shown_version));
        }
        else
        {
            message_label->setText(util::i18n::translate(
                "Version %1 is available for the launcher.", shown_version));
        }
#if defined(Q_OS_MACOS)
        const QString default_details = util::i18n::translate(
            "The installer will open automatically. The launcher will close.");
#else
        const QString default_details = util::i18n::translate(
            "The AppImage will update and restart automatically.");
#endif
        details_label->setText(catalogue_mode
            ? util::i18n::translate("Installed: %1 · Selected: %2")
                .arg(current_version, shown_version)
            : release_message.isEmpty() ? default_details : release_message);
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

    fit_label(title_label, util::layout::scaled(25, window()->size()), qMax(12, util::layout::scaled(18, window()->size())));
    fit_label(message_label, util::layout::scaled(16, window()->size()), qMax(9, util::layout::scaled(12, window()->size())));
    fit_label(details_label, util::layout::scaled(14, window()->size()), qMax(8, util::layout::scaled(11, window()->size())));
    update_button->setAccessibleName(util::i18n::translate("Update launcher now"));
    util::simple_utils::set_button_text(cancel_button, QStringLiteral("CANCEL"));
    cancel_button->setAccessibleName(util::i18n::translate(
        downloading_update ? "Cancel launcher update download" : "Cancel launcher update"));
    close_button->setAccessibleName(util::i18n::translate("Postpone launcher update"));
}

void LauncherUpdate::set_update_button_text(const QString& source)
{
    update_button_source = source;
    update_button_label->setText(util::i18n::translate(source));
    fit_label(update_button_label, util::layout::scaled(21, window()->size()), qMax(10, util::layout::scaled(14, window()->size())));
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
                       util::assets::images[util::assets::Image::BoxUpdate]);
    if (downloading_update || starting_installer)
    {
        util::progress_bar::draw(
            painter,
            local_rect(window()->size(), {74, 194, 472, 21}),
            progress_fraction);
    }
}

bool LauncherUpdate::eventFilter(QObject* object, QEvent* event)
{
    if (object == cancel_button && cancel_button->isEnabled())
    {
        const auto& asset = util::assets::button(util::assets::Button::Cancel);
        util::simple_utils::apply_button_state(
            event, cancel_button, asset.normal, asset.hover, asset.clicked);
    }
    else if (object == update_button && update_button->isEnabled())
    {
        const auto& assets = util::assets::translated_buttons[util::assets::Button::UpdateAvailable];
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
