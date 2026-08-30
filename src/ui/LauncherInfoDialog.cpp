#include "ui/LauncherInfoDialog.hpp"

#include "ui/Assets.hpp"
#include "i18n/LanguageManager.hpp"
#include "ui/Layout.hpp"

#include <QApplication>
#include <QDesktopServices>
#include <QFont>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QSize>
#include <QSizePolicy>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
    QSize launcher_reference_size(const QWidget* widget)
    {
        if (widget && widget->parentWidget())
            return widget->parentWidget()->window()->size();
        return util::layout::win::k_default;
    }
}

LauncherInfoDialog::LauncherInfoDialog(const Page page_, QWidget* parent)
    : QDialog(parent),
      page(page_)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setWindowModality(Qt::ApplicationModal);
    setAttribute(Qt::WA_TranslucentBackground);
    const QSize reference = parent ? parent->window()->size() : util::layout::win::k_default;
    setFixedSize(util::layout::scaled(
        page == Page::Credits ? QSize(610, 425) : QSize(680, 400), reference));
    setup_ui();
    retranslate();

    connect(&util::i18n::LanguageManager::instance(),
            &util::i18n::LanguageManager::language_changed,
            this,
            [this]()
    {
        retranslate();
    });
}

void LauncherInfoDialog::setup_ui()
{
    const QSize reference = launcher_reference_size(this);
    const auto scaled = [&reference](const int value)
    {
        return util::layout::scaled(value, reference);
    };

    auto* panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("launcherInfoPanel"));
    const int panel_inset = scaled(18);
    panel->setGeometry(rect().adjusted(panel_inset, panel_inset, -panel_inset, -panel_inset));
    panel->setStyleSheet(QStringLiteral(
        "QFrame#launcherInfoPanel {"
        "background: rgba(249,244,240,252);"
        "border: 1px solid rgba(79,23,23,78);"
        "border-radius: 0px;"
        "}"
        "QFrame#launcherInfoCard {"
        "background: rgba(238,224,215,150);"
        "border: 1px solid rgba(79,23,23,38);"
        "border-radius: 0px;"
        "}"
        "QPushButton#launcherInfoLink {"
        "background: rgba(255,255,255,156);"
        "color: #4F1717;"
        "border: 1px solid rgba(79,23,23,52);"
        "border-radius: %1px;"
        "padding: %2px %3px;"
        "}"
        "QPushButton#launcherInfoLink:hover {"
        "background: rgba(235,220,211,235);"
        "border-color: rgba(79,23,23,105);"
        "}"
        "QPushButton#launcherInfoLink:pressed {"
        "background: rgba(219,198,186,242);"
        "}"
        "QLabel { background: transparent; }")
        .arg(scaled(6))
        .arg(scaled(8))
        .arg(scaled(12)));

    auto* shadow = new QGraphicsDropShadowEffect(panel);
    shadow->setBlurRadius(scaled(34));
    shadow->setOffset(0, scaled(8));
    shadow->setColor(QColor(43, 28, 19, 94));
    panel->setGraphicsEffect(shadow);

    auto* root = new QVBoxLayout(panel);
    const int horizontal_margin = scaled(page == Page::Credits ? 30 : 34);
    root->setContentsMargins(horizontal_margin, scaled(14), horizontal_margin, scaled(14));
    root->setSpacing(scaled(6));
    root->setAlignment(Qt::AlignTop);

    close_button = new QPushButton(panel);
    close_button->setFlat(true);
    close_button->setFixedSize(util::layout::scaled(util::layout::modal_close::k_hit, reference));
    close_button->setCursor(Qt::PointingHandCursor);
    close_button->setAttribute(Qt::WA_TranslucentBackground);
    close_button->setStyleSheet(QStringLiteral(
        "QPushButton { border: none; background: transparent; padding: 0px; }"
        "QPushButton:hover { border: none; background: transparent; }"
        "QPushButton:pressed { border: none; background: transparent; }"));
    close_button->setIcon(QIcon(util::assets::images[util::assets::Image::CloseSettings]));
    close_button->setIconSize(util::layout::scaled(util::layout::modal_close::k_icon, reference));
    close_button->setGeometry(util::layout::modal_close::rect_in(panel->rect(), reference));
    connect(close_button, &QPushButton::clicked, this, &QDialog::reject);

    logo_label = new QLabel(panel);
    logo_label->setAlignment(Qt::AlignCenter);
    logo_label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    logo_label->setPixmap(util::assets::images[util::assets::Image::SoaLogo]
        .scaled(util::layout::scaled(
                    page == Page::Credits ? QSize(64, 44) : QSize(92, 58), reference),
                Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo_label->setFixedHeight(scaled(page == Page::Credits ? 44 : 58));
    root->addWidget(logo_label);

    title_label = new QLabel(panel);
    title_label->setAlignment(Qt::AlignCenter);
    title_label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    title_label->setFixedHeight(scaled(page == Page::Credits ? 28 : 30));
    QFont title_font = util::assets::fonts[util::assets::Font::EurostileExtraBlack];
    title_font.setPixelSize(qMax(12, scaled(page == Page::Credits ? 22 : 25)));
    title_font.setWeight(QFont::Black);
    title_label->setFont(title_font);
    title_label->setStyleSheet(QStringLiteral("color: #4F1717;"));
    root->addWidget(title_label);

    subtitle_label = new QLabel(panel);
    subtitle_label->setAlignment(Qt::AlignCenter);
    subtitle_label->setWordWrap(true);
    subtitle_label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    subtitle_label->setFixedHeight(scaled(page == Page::Credits ? 20 : 22));
    QFont subtitle_font = util::assets::fonts[util::assets::Font::Inter];
    subtitle_font.setPixelSize(qMax(9, scaled(page == Page::Credits ? 13 : 14)));
    subtitle_font.setWeight(QFont::Medium);
    subtitle_label->setFont(subtitle_font);
    subtitle_label->setStyleSheet(QStringLiteral("color: #5A4636;"));
    root->addWidget(subtitle_label);

    badge_label = new QLabel(panel);
    badge_label->setAlignment(Qt::AlignCenter);
    badge_label->setFixedHeight(scaled(page == Page::Credits ? 26 : 28));
    badge_label->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    QFont badge_font = util::assets::fonts[util::assets::Font::EurostileBlack];
    badge_font.setPixelSize(qMax(8, scaled(12)));
    badge_font.setWeight(QFont::Black);
    badge_label->setFont(badge_font);
    badge_label->setStyleSheet(QStringLiteral(
        "color: #4F1717; background: rgba(216,205,192,112);"
        "border-radius: %1px; padding: %2px %3px;")
        .arg(scaled(6))
        .arg(scaled(2))
        .arg(scaled(12)));
    root->addWidget(badge_label, 0, Qt::AlignHCenter);

    auto* info_card = new QFrame(panel);
    info_card->setObjectName(QStringLiteral("launcherInfoCard"));
    info_card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* info_layout = new QVBoxLayout(info_card);
    info_layout->setContentsMargins(
        scaled(page == Page::Credits ? 18 : 20),
        scaled(10),
        scaled(page == Page::Credits ? 18 : 20),
        scaled(page == Page::Credits ? 16 : 12));
    info_label = new QLabel(info_card);
    info_label->setTextFormat(Qt::RichText);
    info_label->setWordWrap(true);
    info_label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    info_label->setOpenExternalLinks(true);
    info_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    QFont info_font = util::assets::fonts[util::assets::Font::Inter];
    info_font.setPixelSize(qMax(8, scaled(page == Page::Credits ? 12 : 13)));
    info_font.setWeight(QFont::Medium);
    info_label->setFont(info_font);
    info_label->setStyleSheet(QStringLiteral("color: #4F1717;"));
    info_layout->addWidget(info_label);
    info_card->setFixedHeight(scaled(page == Page::Credits ? 200 : 116));
    root->addSpacing(scaled(4));
    root->addWidget(info_card);

    links_widget = new QWidget(panel);
    links_widget->setFixedHeight(scaled(38));
    links_widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* link_row = new QHBoxLayout(links_widget);
    link_row->setContentsMargins(0, 0, 0, 0);
    link_row->setSpacing(scaled(10));
    const auto make_link_button = [this, &scaled]()
    {
        auto* button = new QPushButton(links_widget);
        button->setObjectName(QStringLiteral("launcherInfoLink"));
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(scaled(38));
        QFont button_font = util::assets::fonts[util::assets::Font::EurostileBlack];
        button_font.setPixelSize(qMax(8, scaled(12)));
        button_font.setWeight(QFont::Black);
        button->setFont(button_font);
        return button;
    };

    website_button = make_link_button();
    source_button = make_link_button();
    contact_button = make_link_button();
    link_row->addWidget(website_button);
    link_row->addWidget(source_button);
    link_row->addWidget(contact_button);
    root->addWidget(links_widget);
    links_widget->setVisible(page == Page::About);

    connect(website_button, &QPushButton::clicked, this, []()
    {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://storyofalicia.com")));
    });
    connect(source_button, &QPushButton::clicked, this, []()
    {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/Story-Of-Alicia")));
    });
    connect(contact_button, &QPushButton::clicked, this, []()
    {
        QDesktopServices::openUrl(QUrl(QStringLiteral("mailto:dev@storyofalicia.com")));
    });

    close_button->raise();
}

void LauncherInfoDialog::retranslate()
{
    using util::i18n::translate;

    close_button->setAccessibleName(translate("Close window"));
    website_button->setText(translate("Official Website"));
    source_button->setText(translate("Source Code"));
    contact_button->setText(translate("Contact"));
    website_button->setAccessibleName(website_button->text());
    source_button->setAccessibleName(source_button->text());
    contact_button->setAccessibleName(contact_button->text());

    if (page == Page::About)
    {
        setWindowTitle(translate("About Story of Alicia Launcher"));
        title_label->setText(translate("Story of Alicia Launcher"));
        subtitle_label->setText(translate("The official launcher for Linux and macOS."));
        badge_label->setText(translate("VERSION %1 · QT %2")
            .arg(QApplication::applicationVersion(), QString::fromLatin1(qVersion())));
        info_label->setText(QStringLiteral("<b>%1</b><br>%2<br><br><b>%3</b><br>%4")
            .arg(translate("Everything in one place"),
                 translate("Install, update, verify, repair, and launch both supported Story of Alicia game profiles."),
                 translate("Linux and macOS"),
                 translate("Linux supports Wine and Proton. macOS supports Wine.")));
    }
    else
    {
        setWindowTitle(translate("Credits"));
        title_label->setText(translate("Credits"));
        subtitle_label->setText(translate("Built with help from the Story of Alicia community."));
        badge_label->setText(translate("THANK YOU"));
        info_label->setText(QStringLiteral(
            "<b>%1</b><br>%2<br>"
            "<br><b>%3</b><br>%4<br>"
            "<br><b>%5</b><br>%6<br>"
            "<br><b>%7</b><br>%8")
            .arg(translate("Launcher development"),
                 translate("Story of Alicia team and contributors"),
                 translate("Artwork and branding"),
                 translate("Respective Story of Alicia artists and copyright holders"),
                 translate("Translations and testing"),
                 translate("Community translators, testers, and players"),
                 translate("Open-source technology"),
                 translate("Qt, Wine, spdlog, fmt, and their contributors")));
    }
}

void LauncherInfoDialog::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && event->position().y() <= util::layout::scaled(74, launcher_reference_size(this)))
    {
        dragging = true;
        drag_offset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QDialog::mousePressEvent(event);
}

void LauncherInfoDialog::mouseMoveEvent(QMouseEvent* event)
{
    if (dragging && event->buttons().testFlag(Qt::LeftButton))
    {
        move(event->globalPosition().toPoint() - drag_offset);
        event->accept();
        return;
    }
    QDialog::mouseMoveEvent(event);
}

void LauncherInfoDialog::mouseReleaseEvent(QMouseEvent* event)
{
    dragging = false;
    QDialog::mouseReleaseEvent(event);
}

void LauncherInfoDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);

    QWidget* anchor = parentWidget() ? parentWidget()->window() : nullptr;
    QScreen* target_screen = anchor ? anchor->screen() : screen();
    const QRect available = target_screen
        ? target_screen->availableGeometry()
        : QRect(QPoint(0, 0), size());
    const QPoint center = anchor && anchor->isVisible()
        ? anchor->frameGeometry().center()
        : available.center();
    QPoint position = center - QPoint(width() / 2, height() / 2);
    position.setX(qBound(available.left(), position.x(),
                         qMax(available.left(), available.right() - width() + 1)));
    position.setY(qBound(available.top(), position.y(),
                         qMax(available.top(), available.bottom() - height() + 1)));
    move(position);
}
