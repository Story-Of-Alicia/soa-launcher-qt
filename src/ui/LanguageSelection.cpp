#include "ui/LanguageSelection.hpp"

#include "i18n/LanguageManager.hpp"
#include "ui/Assets.hpp"
#include "ui/ImageDropdown.hpp"
#include "ui/Layout.hpp"
#include "ui/SimpleUtils.hpp"

#include <QDesktopServices>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QUrl>

namespace
{
    constexpr QSize k_box_size {580, 326};

    QRect box_rect(const QSize window_size)
    {
        return soa::ui::layout::centered(k_box_size, window_size, 0, 8);
    }

    QRect local_rect(const QSize window_size, const QRect source)
    {
        return soa::ui::layout::scaled(source, window_size).translated(box_rect(window_size).topLeft());
    }
}

LanguageSelection::LanguageSelection(QWidget* parent)
    : ModalOverlay(parent)
{
    set_keeps_chrome(true);
    setup_controls();
    connect(&soa::i18n::LanguageManager::instance(),
            &soa::i18n::LanguageManager::language_changed,
            this, [this]() { retranslate(); });
    retranslate();
}

void LanguageSelection::setup_controls()
{
    const QSize w = window()->size();

    title_label = new QLabel(this);
    title_label->setAlignment(Qt::AlignCenter);
    title_label->setStyleSheet(QStringLiteral("color:#4F1717; background:transparent;"));
    QFont title_font = soa::ui::assets::fonts[soa::ui::assets::Font::EurostileExtraBlack];
    title_font.setPixelSize(soa::ui::layout::scaled(25, w));
    title_font.setWeight(QFont::Black);
    title_label->setFont(title_font);
    title_label->setGeometry(local_rect(w, {35, 38, 510, 34}));

    message_label = new QLabel(this);
    message_label->setAlignment(Qt::AlignCenter);
    message_label->setWordWrap(true);
    message_label->setStyleSheet(QStringLiteral("color:#5A4636; background:transparent;"));
    QFont body_font = soa::ui::assets::fonts[soa::ui::assets::Font::Inter];
    body_font.setPixelSize(soa::ui::layout::scaled(14, w));
    body_font.setWeight(QFont::Medium);
    message_label->setFont(body_font);
    message_label->setGeometry(local_rect(w, {55, 80, 470, 42}));

    QStringList language_names;
    const auto& manager = soa::i18n::LanguageManager::instance();
    const auto languages = manager.languages();
    language_names.reserve(languages.size());
    for (const auto& language : languages)
        language_names.push_back(language.native_name);

    language_dropdown = new ImageDropdown(language_names, this);
    const QSize dropdown_size = soa::ui::layout::dropdown::box(w);
    language_dropdown->setGeometry(local_rect(
        w, {(k_box_size.width() - 227) / 2, 126, 227, 64}));
    language_dropdown->setFixedSize(dropdown_size);
    language_dropdown->setAccessibleName(soa::i18n::translate("Launcher language"));

    const QString current = manager.current_language();
    for (int index = 0; index < languages.size(); ++index)
    {
        if (languages[index].code == current)
        {
            const QSignalBlocker blocker(language_dropdown);
            language_dropdown->set_index(index);
            break;
        }
    }

    connect(language_dropdown, &ImageDropdown::changed, this,
            [languages](const int index)
    {
        if (index < 0 || index >= languages.size())
            return;
        (void)soa::i18n::LanguageManager::instance().set_language(languages[index].code);
    });

    contribute_label = new QLabel(this);
    contribute_label->setAlignment(Qt::AlignCenter);
    contribute_label->setOpenExternalLinks(false);
    contribute_label->setTextFormat(Qt::RichText);
    contribute_label->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    contribute_label->setStyleSheet(QStringLiteral("color:#7A6858; background:transparent;"));
    QFont small_font = body_font;
    small_font.setPixelSize(soa::ui::layout::scaled(12, w));
    contribute_label->setFont(small_font);
    contribute_label->setGeometry(local_rect(w, {40, 201, 500, 30}));
    connect(contribute_label, &QLabel::linkActivated, this, [](const QString& link)
    {
        QDesktopServices::openUrl(QUrl(link));
    });

    const auto& button_assets = soa::ui::assets::translated_buttons[soa::ui::assets::Button::Agree];
    const QSize button_size = soa::ui::layout::scaled(button_assets.normal.size(), w);
    continue_button = soa::ui::simple_utils::make_flat_button(this);
    continue_button->setIcon(QIcon(button_assets.normal));
    continue_button->setIconSize(button_size);
    continue_button->setGeometry(local_rect(
        w, {(k_box_size.width() - 257) / 2, 247, 257, 40}));
    continue_button->setAccessibleName(soa::i18n::translate("Continue with selected language"));
    continue_button->installEventFilter(this);

    continue_label = new QLabel(continue_button);
    continue_label->setAttribute(Qt::WA_TransparentForMouseEvents);
    continue_label->setAlignment(Qt::AlignCenter);
    continue_label->setStyleSheet(QStringLiteral("color:#FFFFFF; background:transparent;"));
    QFont button_font = soa::ui::assets::fonts[soa::ui::assets::Font::EurostileExtraBlack];
    button_font.setPixelSize(soa::ui::layout::scaled(13, w));
    button_font.setWeight(QFont::Black);
    continue_label->setFont(button_font);
    continue_label->setGeometry(continue_button->rect());
    continue_label->raise();

    connect(continue_button, &QPushButton::clicked, this, &LanguageSelection::accepted);
}

void LanguageSelection::retranslate()
{
    const auto& manager = soa::i18n::LanguageManager::instance();
    const auto languages = manager.languages();
    const QString current = manager.current_language();
    for (int index = 0; index < languages.size(); ++index)
    {
        if (languages[index].code == current)
        {
            const QSignalBlocker blocker(language_dropdown);
            language_dropdown->set_index(index);
            break;
        }
    }

    title_label->setText(soa::i18n::translate("CHOOSE YOUR LANGUAGE"));
    message_label->setText(soa::i18n::translate(
        "Choose the language you want to use in the launcher."));
    language_dropdown->setAccessibleName(soa::i18n::translate("Launcher language"));
    continue_label->setText(soa::i18n::translate("CONTINUE"));
    continue_button->setAccessibleName(soa::i18n::translate("Continue with selected language"));

    const QString prompt = soa::i18n::translate("Don't see your language?").toHtmlEscaped();
    const QString link_text = soa::i18n::translate("Help translate the launcher.").toHtmlEscaped();
    contribute_label->setText(QStringLiteral(
        "%1 <a href=\"https://github.com/Story-Of-Alicia/soa-launcher-qt/blob/main/CONTRIBUTING.md\" "
        "style=\"color:#2FB4E0; text-decoration:none; font-weight:700;\">%2</a>")
        .arg(prompt, link_text));
}

void LanguageSelection::paint_content(QPainter& painter)
{
    painter.drawPixmap(box_rect(window()->size()),
                       soa::ui::assets::images[soa::ui::assets::Image::BoxModal]);
}

bool LanguageSelection::eventFilter(QObject* object, QEvent* event)
{
    if (object == continue_button)
    {
        const auto& assets = soa::ui::assets::translated_buttons[soa::ui::assets::Button::Agree];
        soa::ui::simple_utils::apply_button_state(
            event, continue_button, assets.normal, assets.hover, assets.clicked);
    }
    return QWidget::eventFilter(object, event);
}
