#include "ui/AliciaChooser.hpp"
#include "ui/Layout.hpp"
#include "ui/SimpleUtils.hpp"
#include "config/Config.hpp"
#include "i18n/LanguageManager.hpp"
#include "auth/AuthHandler.hpp"
#include "runtime/Shell.hpp"
#include "ui/InstallState.hpp"
#include "ui/LauncherDialog.hpp"

#include <QCheckBox>
#include <QFontMetrics>
#include <QGraphicsDropShadowEffect>
#include <QSignalBlocker>
#include <QTimer>

using soa::config::Config;
using soa::ui::Stage;

namespace
{
    QString note_box_style(const QSize window_size)
    {
        return QStringLiteral(
            "QLabel {"
            "background: rgba(246, 231, 223, 0.92);"
            "border: 1px solid rgba(160, 119, 98, 0.42);"
            "border-radius: 0px;"
            "color: #5A4636;"
            "font-family: 'Inter';"
            "font-size: %1px;"
            "padding: %2px %3px;"
            "}")
            .arg(qMax(8, soa::ui::layout::scaled(12, window_size)))
            .arg(soa::ui::layout::scaled(8, window_size))
            .arg(soa::ui::layout::scaled(12, window_size));
    }

    QString sign_out_link_style(const QSize window_size)
    {
        Q_UNUSED(window_size);
        return QStringLiteral(
            "QPushButton {"
            "background: transparent;"
            "border: none;"
            "color: #8E8170;"
            "text-align: right;"
            "padding-right: 0px;"
            "}"
            "QPushButton:hover { color: #4F1717; }"
            "QPushButton:disabled { color: #A89B8D; }");
    }

    QString reset_link_style(const QSize window_size)
    {
        return QStringLiteral(
            "QPushButton {"
            "background: transparent;"
            "border: none;"
            "color: #9E8E7E;"
            "font-family: 'Inter';"
            "font-weight: 700;"
            "font-size: %1px;"
            "}"
            "QPushButton:hover { color: #7E6E5E; }")
            .arg(qMax(9, soa::ui::layout::scaled(15, window_size)));
    }

    QString checkbox_style(const QSize window_size, const int spacing)
    {
        return QStringLiteral(
            "QCheckBox { background: transparent; color: #4F1717; spacing: %1px; }"
            "QCheckBox:hover { color: #321010; }"
            "QCheckBox::indicator { width: %2px; height: %3px; }"
            "QCheckBox::indicator:unchecked { border-image: url(:/assets/checkbox.png) 0 0 0 0 stretch stretch; }"
            "QCheckBox::indicator:checked { border-image: url(:/assets/checkbox-ticked.png) 0 0 0 0 stretch stretch; }")
            .arg(soa::ui::layout::scaled(spacing, window_size))
            .arg(qMax(12, soa::ui::layout::scaled(19, window_size)))
            .arg(qMax(11, soa::ui::layout::scaled(18, window_size)));
    }

    void add_soft_shadow(QWidget* widget, const qreal blur = 22.0, const qreal y = 7.0,
                         const QColor& color = QColor(65, 39, 25, 72))
    {
        const QSize window_size = widget && widget->window()
            ? widget->window()->size() : soa::ui::layout::win::k_default;
        auto* effect = new QGraphicsDropShadowEffect(widget);
        effect->setBlurRadius(soa::ui::layout::scaled(qRound(blur), window_size));
        effect->setOffset(0.0, soa::ui::layout::scaled(qRound(y), window_size));
        effect->setColor(color);
        widget->setGraphicsEffect(effect);
    }

    void fit_acknowledgement_label(QLabel* label, const QString& plain_text,
                                   const QSize window_size)
    {
        if (!label)
            return;

        QFont font = soa::ui::assets::fonts[soa::ui::assets::Font::Inter];
        int pixel_size = qMax(8, soa::ui::layout::scaled(13, window_size));
        while (pixel_size > qMax(8, soa::ui::layout::scaled(10, window_size)))
        {
            font.setPixelSize(pixel_size);
            font.setWeight(QFont::Medium);
            if (QFontMetrics(font).horizontalAdvance(plain_text)
                <= qMax(1, label->width()))
            {
                break;
            }
            --pixel_size;
        }
        font.setPixelSize(pixel_size);
        font.setWeight(QFont::Medium);
        label->setFont(font);
    }

    QString welcome_message(const soa::common::game::GameVersion version, const QString& required_action)
    {
        const QString game_name = version == soa::common::game::GameVersion::Alicia2
            ? QStringLiteral("Story of Alicia 2.0")
            : QStringLiteral("Story of Alicia");
        return soa::i18n::translate(
            "Welcome to %1. To participate in the playtest, you have to first %2.")
            .arg(game_name, required_action);
    }

    void set_dynamic_text(QLabel* label, const QString& source, const bool uppercase = false)
    {
        if (!label)
            return;
        label->setProperty("soa_i18n_text_source", source);
        label->setProperty("soa_i18n_text_uppercase", uppercase);
        const QString translated = soa::i18n::translate(source);
        label->setText(uppercase ? translated.toUpper() : translated);
    }
}

AliciaChooser::AliciaChooser(AuthHandler* auth_, soa::runtime::Shell* shell_,
                   soa::ui::InstallState* install_state_, QWidget* parent)
    : QWidget(parent),
      auth(auth_),
      shell(shell_),
      install_state(install_state_),
      game_version(Config::instance().game_version())
{
    const QSize w = window()->size();
    setFixedSize(soa::ui::layout::alicia_chooser::box(w));
    add_soft_shadow(this, 30.0, 9.0, QColor(43, 28, 19, 88));

    setup_title();
    setup_settings_button();
    setup_download_state();
    setup_login_state();
    setup_waiting_state();
    setup_signedin_state();

    retranslate_dynamic_text();
    refresh_keep_signed_in();
    refresh_session_banner();
    connect(&Config::instance(), &Config::changed,
            this, &AliciaChooser::refresh_keep_signed_in);
    connect(&Config::instance(), &Config::changed,
            this, &AliciaChooser::refresh_session_banner);
    connect(&Config::instance(), &Config::changed,
            this, &AliciaChooser::refresh_acknowledgements);
    connect(auth, &AuthHandler::authenticated, this,
            [this](const QString&, const QString&, const QString&)
    {
        refresh_session_banner();
        QTimer::singleShot(0, this, [this]()
        {
            refresh_session_banner();
            refresh_enter_enabled();
        });
    });

    reset_path_button = new QPushButton("RESET LAUNCHER SETTINGS", this);
    reset_path_button->setCursor(Qt::PointingHandCursor);
    reset_path_button->setAccessibleName(QStringLiteral("Reset launcher settings"));
    reset_path_button->setStyleSheet(reset_link_style(w));
    reset_path_button->setGeometry(soa::ui::layout::alicia_chooser::reset(w));
    reset_path_button->setToolTip(soa::i18n::translate("Reset launcher settings and sign-in without deleting the shared prefix or either game"));
    connect(reset_path_button, &QPushButton::clicked, this, [this]()
    {
        emit reset_config_requested();
    });

    QTimer::singleShot(0, this, [this]()
    {
        set_warning(install_state->warning_message());
    });

    retranslate_dynamic_text();
    on_stage_changed(install_state->stage());
    connect(install_state, &soa::ui::InstallState::stage_changed,
            this, &AliciaChooser::on_stage_changed);
    connect(install_state, &soa::ui::InstallState::warning_changed,
            this, &AliciaChooser::set_warning);
    connect(&soa::i18n::LanguageManager::instance(),
            &soa::i18n::LanguageManager::language_changed, this,
            [this]()
    {
        retranslate_dynamic_text();
        on_stage_changed(current_stage);
        set_warning(install_state->warning_message());
    });
}

void AliciaChooser::set_game_version(const soa::common::game::GameVersion version)
{
    if (game_version == version) return;

    game_version = version;
    refresh_game_text();
    on_stage_changed(current_stage);
}

AliciaChooser::State AliciaChooser::state_for(const Stage stage)
{
    switch (stage)
    {
        case Stage::NeedsAuth:      return State::Login;
        case Stage::Authenticating: return State::Waiting;
        case Stage::Launching:
        case Stage::Running:
        case Stage::Ready:          return State::SignedIn;
        default:                    return State::Download;
    }
}

void AliciaChooser::on_stage_changed(const Stage stage)
{
    current_stage = stage;
    const State next = state_for(stage);

    QString message;
    switch (stage)
    {
        case Stage::Probing:         message = soa::i18n::translate("Checking launcher state..."); break;
        case Stage::NeedsPrerequisites:
            message = welcome_message(game_version, soa::i18n::translate("complete the easy setup assistant"));
            break;
        case Stage::NeedsRuntime:
#if defined(Q_OS_MACOS)
            message = welcome_message(
                game_version, soa::i18n::translate("choose Wine"));
#else
            message = welcome_message(game_version, soa::i18n::translate("choose Wine or Proton"));
#endif
            break;
        case Stage::NeedsPrefix:
            message = soa::i18n::translate("The shared Wine prefix needs to be created before installing the game.");
            break;
        case Stage::PrefixBroken:
            message = soa::i18n::translate("The shared Wine prefix is incomplete and needs repair.");
            break;
        case Stage::SettingUpPrefix:
            message = soa::i18n::translate("Preparing the shared Wine prefix...");
            break;
        case Stage::NeedsDownload:
            message = welcome_message(game_version, soa::i18n::translate("download the game"));
            break;
        case Stage::CheckingUpdate:  message = soa::i18n::translate("Checking this game for updates..."); break;
        case Stage::Downloading:     message = soa::i18n::translate("Downloading and verifying game files..."); break;
        case Stage::NeedsUpdate:     message = soa::i18n::translate("A game update is available and must be installed before launch."); break;
        case Stage::Updating:        message = soa::i18n::translate("Updating and verifying game files..."); break;
        case Stage::NeedsRules:      message = soa::i18n::translate("The game is installed. Review and accept the playtest rules before signing in."); break;
        case Stage::Failed:          message = install_state->error_message(); break;
        default: break;
    }
    if (!message.isEmpty())
        message_label->setText(message);

    const bool actionable = stage == Stage::NeedsRuntime || stage == Stage::NeedsPrefix
        || stage == Stage::PrefixBroken || stage == Stage::NeedsDownload
        || stage == Stage::NeedsUpdate;
    const bool download_active = stage == Stage::SettingUpPrefix
        || stage == Stage::CheckingUpdate
        || stage == Stage::Downloading
        || stage == Stage::Updating;
    soa::ui::simple_utils::set_button_active(download_button, download_active);
    soa::ui::simple_utils::set_button_enabled(download_button, actionable);
    const auto action = stage == Stage::NeedsUpdate || stage == Stage::Updating
        ? soa::ui::assets::Button::UpdateAvailable
        : soa::ui::assets::Button::DownloadGame;
    soa::ui::simple_utils::set_button_asset(download_button, action);
    soa::ui::simple_utils::set_button_text(
        download_button, stage == Stage::NeedsUpdate || stage == Stage::Updating
            ? QStringLiteral("UPDATE AVAILABLE")
            : QStringLiteral("DOWNLOAD GAME"));

    set_state(next);
    refresh_session_banner();
    refresh_enter_enabled();
}

void AliciaChooser::setup_title()
{
    const QSize w = window()->size();

    title_label = new QLabel(this);
    title_label->setAlignment(Qt::AlignCenter);
    title_label->setTextFormat(Qt::PlainText);

    QFont title_font = soa::ui::assets::fonts[soa::ui::assets::Font::EurostileExtraBlack];
    title_font.setPixelSize(soa::ui::layout::scaled(soa::ui::layout::text::k_modal_header, w));
    title_font.setWeight(QFont::Black);
    title_label->setFont(title_font);
    title_label->setStyleSheet("color: #4F1717; background: transparent;");
    title_label->setGeometry(soa::ui::layout::alicia_chooser::title(w));
}

void AliciaChooser::setup_settings_button()
{
    const QSize w = window()->size();
    const QRect button = soa::ui::layout::alicia_chooser::settings_button(w);

    settings_button = soa::ui::simple_utils::make_flat_button(this);

    const QPixmap scaled = soa::ui::assets::images[soa::ui::assets::Image::SettingsButton]
        .scaled(button.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    settings_button->setIcon(QIcon(scaled));
    settings_button->setIconSize(button.size());
    settings_button->setGeometry(button);
    settings_button->setAccessibleName(QStringLiteral("Open launcher settings"));

    connect(settings_button, &QPushButton::clicked, this, [this] { emit settings_requested(); });
}

void AliciaChooser::setup_download_state()
{
    const QSize w = window()->size();

    message_label = new QLabel(this);
    message_label->setAlignment(Qt::AlignCenter);
    message_label->setWordWrap(true);
    message_label->setTextFormat(Qt::PlainText);

    QFont msg_font = soa::ui::assets::fonts[soa::ui::assets::Font::Inter];
    msg_font.setPixelSize(soa::ui::layout::scaled(soa::ui::layout::text::k_body, w));
    msg_font.setWeight(QFont::Medium);
    message_label->setFont(msg_font);
    message_label->setStyleSheet(QStringLiteral(
        "color: #4F1717; background: transparent; padding: 0px %1px;")
        .arg(soa::ui::layout::scaled(12, w)));
    message_label->setGeometry(soa::ui::layout::alicia_chooser::message(w));

    download_button = new QPushButton(this);
    download_button->setFlat(true);
    download_button->setCursor(Qt::PointingHandCursor);
    download_button->setText("");
    download_button->setStyleSheet("border: none;  background: transparent;");

    const QPixmap& normal = soa::ui::assets::button(soa::ui::assets::Button::DownloadGame).normal;

    const int bw = soa::ui::layout::alicia_chooser::dl_button_w(w);
    const int bh = qRound(bw * static_cast<double>(normal.height()) / normal.width());
    const int bx = soa::ui::layout::alicia_chooser::dl_button_x(w);
    const int by = soa::ui::layout::alicia_chooser::dl_button_y(w);

    download_button->setIcon(QIcon(normal));
    download_button->setIconSize(QSize(bw, bh));
    download_button->setGeometry(bx, by, bw, bh);
    download_button->setProperty("soa_button_stretch_asset", true);
    QFont download_font = soa::ui::assets::fonts[soa::ui::assets::Font::EurostileExtraBlack];
    download_font.setPixelSize(soa::ui::layout::scaled(20, w));
    download_font.setWeight(QFont::Black);
    soa::ui::simple_utils::add_button_text(download_button, soa::ui::assets::Button::DownloadGame, QStringLiteral("DOWNLOAD GAME"), download_font);
    download_button->setAccessibleName(QStringLiteral("Open the required game setup action"));
    download_button->installEventFilter(this);
    connect(download_button, &QPushButton::clicked, this, [this]()
    {
        if (download_button->isEnabled()) emit download_triggered();
    });
}

void AliciaChooser::setup_login_state()
{
    const QSize w = window()->size();

    const QRect dr = soa::ui::layout::alicia_chooser::discord_button(w);
    const QPixmap& discord_normal = soa::ui::assets::button(soa::ui::assets::Button::Discord).normal;
    const int dw = dr.width();
    const int dh = qRound(dw * static_cast<double>(discord_normal.height()) / discord_normal.width());

    discord_button = new QPushButton(this);
    discord_button->setFlat(true);
    discord_button->setCursor(Qt::PointingHandCursor);
    discord_button->setText("");
    discord_button->setStyleSheet("border: none;  background: transparent;");
    discord_button->setIcon(QIcon(discord_normal));
    discord_button->setIconSize(QSize(dw, dh));
    discord_button->setGeometry(dr.x(), dr.y(), dw, dh);
    discord_button->setProperty("soa_button_stretch_asset", true);
    QFont discord_font = soa::ui::assets::fonts[soa::ui::assets::Font::EurostileExtraBlack];
    discord_font.setPixelSize(soa::ui::layout::scaled(18, w));
    discord_font.setWeight(QFont::Black);
    soa::ui::simple_utils::add_button_text(
        discord_button, soa::ui::assets::Button::Discord, QStringLiteral("PROCEED WITH DISCORD"), discord_font, QColor(Qt::white),
        QPoint(soa::ui::layout::scaled(18, w), 0));
    discord_button->setAccessibleName(QStringLiteral("Proceed with Discord"));
    discord_button->installEventFilter(this);
    connect(discord_button, &QPushButton::clicked, auth, &AuthHandler::open_login);

    keep_signed_button = new QCheckBox(QStringLiteral("Keep me signed in"), this);
    keep_signed_button->setCursor(Qt::PointingHandCursor);
    keep_signed_button->setAccessibleName(QStringLiteral("Keep me signed in after the launcher closes"));
    keep_signed_button->setGeometry(soa::ui::layout::alicia_chooser::keep_signed_in(w));
    QFont keep_font = soa::ui::assets::fonts[soa::ui::assets::Font::Inter];
    keep_font.setPixelSize(qMax(8, soa::ui::layout::scaled(13, w)));
    keep_font.setWeight(QFont::Medium);
    keep_signed_button->setFont(keep_font);
    keep_signed_button->setStyleSheet(checkbox_style(w, 5));
    connect(keep_signed_button, &QCheckBox::toggled, this, [](const bool checked)
    {
        Config::instance().set_keep_signed_in(checked);
    });

    disclaimer_label = new QLabel(this);
    disclaimer_label->setWordWrap(true);
    disclaimer_label->setTextFormat(Qt::RichText);
    disclaimer_label->setOpenExternalLinks(true);
    disclaimer_label->setText(
        soa::i18n::translate(
            "By clicking the \u201CProceed with Discord\u201D button, you acknowledge that your "
            "Discord ID will be stored on our database servers indefinitely for identification "
            "and service purposes. This data can be removed upon request by emailing %1.")
            .arg(QStringLiteral("<a href=\"mailto:dev@storyofalicia.com\" "
                                "style=\"color:#2FB4E0;\">dev@storyofalicia.com</a>")));
    disclaimer_label->setStyleSheet(note_box_style(w));
    disclaimer_label->setGeometry(soa::ui::layout::alicia_chooser::disclaimer(w));
    add_soft_shadow(disclaimer_label, 18.0, 6.0, QColor(64, 40, 27, 62));
}

void AliciaChooser::setup_waiting_state()
{
    const QSize w = window()->size();

    waiting_title = new QLabel("WAITING FOR BROWSER AUTHENTICATION", this);
    waiting_title->setAlignment(Qt::AlignCenter);
    waiting_title->setTextFormat(Qt::PlainText);
    QFont wf = soa::ui::assets::fonts[soa::ui::assets::Font::EurostileBlack];
    wf.setPixelSize(soa::ui::layout::scaled(16, w));
    wf.setWeight(QFont::Black);
    waiting_title->setFont(wf);
    waiting_title->setStyleSheet("color: #4F1717; background: transparent;");
    waiting_title->setGeometry(soa::ui::layout::alicia_chooser::waiting_title(w));

    steps_label = new QLabel(this);
    steps_label->setWordWrap(true);
    steps_label->setTextFormat(Qt::RichText);
    steps_label->setText(
        QStringLiteral("<b>1.</b>&nbsp; %1<br><b>2.</b>&nbsp; %2<br>"
                       "<b>3.</b>&nbsp; %3<br><b>4.</b>&nbsp; %4")
            .arg(soa::i18n::translate("Open the exact copied sign-in link in the browser you want to use."),
                 soa::i18n::translate("Sign in with Discord and authorize the launcher."),
                 soa::i18n::translate("Click %1 when prompted.")
                     .arg(QStringLiteral("<b>“Open Story of Alicia Launcher”</b>")),
                 soa::i18n::translate("If login did not work, cancel and try again.")));
    steps_label->setStyleSheet(note_box_style(w));
    steps_label->setGeometry(soa::ui::layout::alicia_chooser::steps(w));
    add_soft_shadow(steps_label, 18.0, 6.0, QColor(64, 40, 27, 62));

    try_again_button = new QPushButton(QStringLiteral("Cancel / Try again"), this);
    try_again_button->setCursor(Qt::PointingHandCursor);
    try_again_button->setAccessibleName(QStringLiteral("Cancel Discord login and try again"));
    try_again_button->setGeometry(soa::ui::layout::alicia_chooser::try_again(w));
    QFont retry_font = soa::ui::assets::fonts[soa::ui::assets::Font::Inter];
    retry_font.setPixelSize(qMax(8, soa::ui::layout::scaled(11, w)));
    retry_font.setWeight(QFont::Medium);
    retry_font.setUnderline(true);
    try_again_button->setFont(retry_font);
    try_again_button->setStyleSheet(
        "QPushButton { background: transparent; border: none; color: #988776; }"
        "QPushButton:hover { color: #6F5F50; }");
    connect(try_again_button, &QPushButton::clicked, auth, &AuthHandler::cancel_login);
}

void AliciaChooser::setup_signedin_state()
{
    const QSize w = window()->size();

    const QString acknowledgement_style = checkbox_style(w, 0);

    const auto configure_acknowledgement_box = [this, &acknowledgement_style](QCheckBox* checkbox)
    {
        checkbox->setFocusPolicy(Qt::StrongFocus);
        checkbox->setStyleSheet(acknowledgement_style);
        connect(checkbox, &QCheckBox::toggled, this, [this]()
        {
            refresh_enter_enabled();
        });
    };

    signed_bug_checkbox = new QCheckBox(this);
    signed_bug_checkbox->setGeometry(soa::ui::layout::alicia_chooser::signed_bug_checkbox(w));
    signed_bug_checkbox->setAccessibleName(QStringLiteral("Playtest status acknowledged"));
    configure_acknowledgement_box(signed_bug_checkbox);

    signed_bug_label = new QLabel(this);
    signed_bug_label->setTextFormat(Qt::PlainText);
    signed_bug_label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    signed_bug_label->setStyleSheet(QStringLiteral(
        "QLabel { color: #4F1717; background: transparent; }"));
    signed_bug_label->setGeometry(soa::ui::layout::alicia_chooser::signed_bug_text(w));

    signed_rules_checkbox = new QCheckBox(this);
    signed_rules_checkbox->setGeometry(soa::ui::layout::alicia_chooser::signed_rules_checkbox(w));
    signed_rules_checkbox->setAccessibleName(QStringLiteral("Server rules acknowledged"));
    configure_acknowledgement_box(signed_rules_checkbox);

    signed_rules_label = new QLabel(this);
    signed_rules_label->setTextFormat(Qt::RichText);
    signed_rules_label->setOpenExternalLinks(true);
    signed_rules_label->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    signed_rules_label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    signed_rules_label->setStyleSheet(QStringLiteral(
        "QLabel { color: #4F1717; background: transparent; }"
        "QLabel a { color: #2FB4E0; text-decoration: none; }"));
    signed_rules_label->setGeometry(soa::ui::layout::alicia_chooser::signed_rules_text(w));

    signed_in_banner = new QLabel(this);
    const QRect banner_rect = soa::ui::layout::alicia_chooser::signed_in_banner(w);
    signed_in_banner->setGeometry(banner_rect);
    signed_in_banner->setPixmap(
        soa::ui::assets::images[soa::ui::assets::Image::SignedInAs].scaled(
            banner_rect.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    signed_in_banner->setScaledContents(false);

    signed_in_icon = new QLabel(this);
    signed_in_icon->setGeometry(soa::ui::layout::alicia_chooser::signed_in_icon(w));
    signed_in_icon->setAlignment(Qt::AlignCenter);
    signed_in_icon->setPixmap(
        soa::ui::assets::images[soa::ui::assets::Image::DiscordMark].scaled(
            signed_in_icon->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

    signed_in_label = new QLabel(this);
    signed_in_label->setTextFormat(Qt::PlainText);
    signed_in_label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    signed_in_label->setGeometry(soa::ui::layout::alicia_chooser::signed_in_text(w));
    QFont signed_in_font = soa::ui::assets::fonts[soa::ui::assets::Font::EurostileExtraBlack];
    signed_in_font.setPixelSize(qMax(9, soa::ui::layout::scaled(14, w)));
    signed_in_font.setWeight(QFont::Black);
    signed_in_label->setFont(signed_in_font);
    signed_in_label->setStyleSheet(QStringLiteral(
        "QLabel { color: #4F1717; background: transparent; }"));

    sign_out_button = new QPushButton(this);
    sign_out_button->setFlat(true);
    sign_out_button->setCursor(Qt::PointingHandCursor);
    sign_out_button->setGeometry(soa::ui::layout::alicia_chooser::sign_out(w));
    sign_out_button->setStyleSheet(sign_out_link_style(w));
    QFont sign_out_font = soa::ui::assets::fonts[soa::ui::assets::Font::Inter];
    sign_out_font.setPixelSize(qMax(8, soa::ui::layout::scaled(9, w)));
    sign_out_font.setWeight(QFont::Bold);
    sign_out_font.setUnderline(true);
    sign_out_button->setFont(sign_out_font);
    sign_out_button->setAccessibleName(soa::i18n::translate("Sign out"));
    connect(sign_out_button, &QPushButton::clicked, this, [this]()
    {
        if (current_stage == Stage::Launching || current_stage == Stage::Running)
            return;
        sign_out_button->setEnabled(false);
        Config::instance().clear_auth();
    });

    const QRect er = soa::ui::layout::alicia_chooser::enter_button(w);
    const auto enter_asset = game_version == soa::common::game::GameVersion::Alicia2
        ? soa::ui::assets::Button::EnterAlicia2
        : soa::ui::assets::Button::Enter;
    const QPixmap& enter_normal = soa::ui::assets::button(enter_asset).normal;
    const int ew = er.width();
    const int eh = qRound(ew * static_cast<double>(enter_normal.height()) / enter_normal.width());

    enter_button = new QPushButton(this);
    enter_button->setFlat(true);
    enter_button->setCursor(Qt::PointingHandCursor);
    enter_button->setText("");
    enter_button->setStyleSheet("border: none;  background: transparent;");
    enter_button->setIcon(QIcon(enter_normal));
    enter_button->setIconSize(QSize(ew, eh));
    enter_button->setGeometry(er.x(), er.y(), ew, eh);
    enter_button->setProperty("soa_button_stretch_asset", true);
    QFont enter_font = soa::ui::assets::fonts[soa::ui::assets::Font::EurostileExtraBlack];
    enter_font.setPixelSize(soa::ui::layout::scaled(20, w));
    enter_font.setWeight(QFont::Black);
    soa::ui::simple_utils::add_button_text(enter_button, enter_asset, QStringLiteral("ENTER THE PLAYTEST"), enter_font);
    enter_button->setEnabled(false);
    enter_button->setAccessibleName(QStringLiteral("Enter the playtest"));
    enter_button->installEventFilter(this);
    refresh_acknowledgements();
    connect(enter_button, &QPushButton::clicked, this, [this]()
    {
        if (!enter_button->isEnabled())
            return;

        auto& config = Config::instance();
        if (!config.rules_accepted())
        {
            if (!signed_bug_checkbox->isChecked() || !signed_rules_checkbox->isChecked())
                return;
            config.set_rules_accepted(true);
        }

        shell->run_game(config.username(), config.token());
    });
}

void AliciaChooser::set_state(const State next_state)
{
    state = next_state;
    apply_state_visibility();
    if (next_state == State::SignedIn) refresh_enter_enabled();
}

void AliciaChooser::apply_state_visibility()
{
    const bool download = state == State::Download;
    const bool login    = state == State::Login;
    const bool waiting  = state == State::Waiting;
    const bool signedin = state == State::SignedIn;

    download_button->setVisible(download);
    message_label->setVisible(download);

    discord_button->setVisible(login);
    keep_signed_button->setVisible(login);
    disclaimer_label->setVisible(login);

    waiting_title->setVisible(waiting);
    steps_label->setVisible(waiting);
    try_again_button->setVisible(waiting);

    signed_bug_checkbox->setVisible(signedin);
    signed_bug_label->setVisible(signedin);
    signed_rules_checkbox->setVisible(signedin);
    signed_rules_label->setVisible(signedin);
    signed_in_banner->setVisible(signedin);
    signed_in_icon->setVisible(signedin);
    signed_in_label->setVisible(signedin);
    sign_out_button->setVisible(signedin);
    enter_button->setVisible(signedin);

    const bool game_active =
        current_stage == Stage::Launching || current_stage == Stage::Running;
    settings_button->setEnabled(true);
    settings_button->setCursor(Qt::PointingHandCursor);
    settings_button->setToolTip(QString());

    reset_path_button->setVisible(!download);
    reset_path_button->setEnabled(!game_active);
    reset_path_button->setToolTip(reset_path_button->isEnabled()
        ? soa::i18n::translate(
              "Reset launcher settings and sign-in without deleting the shared prefix or either game")
        : soa::i18n::translate("Launcher settings cannot be reset while Alicia is active"));

    sign_out_button->setEnabled(!game_active);
    sign_out_button->setCursor(game_active ? Qt::ArrowCursor : Qt::PointingHandCursor);
    sign_out_button->setToolTip(game_active
        ? soa::i18n::translate("Sign out is unavailable while Alicia is active")
        : soa::i18n::translate("Sign out of Discord"));
}

void AliciaChooser::refresh_enter_enabled()
{
    if (!enter_button)
        return;

    const bool acknowledged = Config::instance().rules_accepted()
        || (signed_bug_checkbox && signed_bug_checkbox->isChecked()
            && signed_rules_checkbox && signed_rules_checkbox->isChecked());
    const bool ready = current_stage == Stage::Ready && acknowledged;
    const bool active = current_stage == Stage::Launching || current_stage == Stage::Running;
    soa::ui::simple_utils::set_button_active(enter_button, active);
    soa::ui::simple_utils::set_button_enabled(enter_button, ready);
    enter_button->setToolTip(current_stage == Stage::Running
        ? soa::i18n::translate("Alicia is already running")
        : current_stage == Stage::Launching
            ? soa::i18n::translate("Alicia is starting")
            : QString());
}

void AliciaChooser::refresh_acknowledgements()
{
    if (!signed_bug_checkbox || !signed_rules_checkbox)
        return;

    const bool accepted = Config::instance().rules_accepted();
    const QSignalBlocker bug_blocker(signed_bug_checkbox);
    const QSignalBlocker rules_blocker(signed_rules_checkbox);

    if (accepted)
    {
        signed_bug_checkbox->setChecked(true);
        signed_rules_checkbox->setChecked(true);
    }
    else if (rules_accepted_cached)
    {
        signed_bug_checkbox->setChecked(false);
        signed_rules_checkbox->setChecked(false);
    }
    rules_accepted_cached = accepted;

    signed_bug_checkbox->setAttribute(Qt::WA_TransparentForMouseEvents, accepted);
    signed_rules_checkbox->setAttribute(Qt::WA_TransparentForMouseEvents, accepted);
    signed_bug_checkbox->setFocusPolicy(accepted ? Qt::NoFocus : Qt::StrongFocus);
    signed_rules_checkbox->setFocusPolicy(accepted ? Qt::NoFocus : Qt::StrongFocus);
    refresh_enter_enabled();
}

void AliciaChooser::refresh_session_banner()
{
    if (!signed_in_label || !enter_button)
        return;

    QString source;
    if (current_stage == Stage::Launching)
    {
        source = QStringLiteral("Starting Alicia…");
        signed_in_label->setAccessibleName(soa::i18n::translate("Alicia is starting"));
        enter_button->setAccessibleDescription(
            soa::i18n::translate("Disabled while Alicia is starting"));
    }
    else if (current_stage == Stage::Running)
    {
        source = QStringLiteral("Alicia is running");
        signed_in_label->setAccessibleName(soa::i18n::translate("Alicia is running"));
        enter_button->setAccessibleDescription(
            soa::i18n::translate("Disabled while Alicia is running"));
    }
    else
    {
        const QString account = Config::instance().display_name().trimmed().isEmpty()
            ? Config::instance().username().trimmed()
            : Config::instance().display_name().trimmed();
        source = account.isEmpty()
            ? QStringLiteral("Signed in")
            : QStringLiteral("Signed in as %1").arg(account);
        signed_in_label->setAccessibleName(
            account.isEmpty() ? soa::i18n::translate("Signed in")
                              : soa::i18n::translate("Signed in as %1").arg(account));
        enter_button->setAccessibleDescription(
            soa::i18n::translate("Start the selected Alicia playtest"));
    }

    set_dynamic_text(signed_in_label, source, true);
    signed_in_label->setToolTip(signed_in_label->text().trimmed());
    if (sign_out_button)
    {
        sign_out_button->setProperty("soa_i18n_text_source", QStringLiteral("Sign out"));
        sign_out_button->setProperty("soa_i18n_text_uppercase", true);
        sign_out_button->setText(soa::i18n::translate("Sign out").toUpper());
    }
}

void AliciaChooser::set_warning(const QString& message)
{
    const QString source = message.trimmed();
    if (source.isEmpty())
    {
        displayed_warning.clear();
        if (warning_dialog)
            warning_dialog->close();
        warning_dialog.clear();
        return;
    }

    if (source == displayed_warning && warning_dialog)
    {
        warning_dialog->raise();
        warning_dialog->activateWindow();
        return;
    }

    if (warning_dialog)
        warning_dialog->close();

    displayed_warning = source;
    auto* dialog = LauncherDialog::open_message(
        this,
        LauncherDialog::Tone::Warning,
        QStringLiteral("Launcher Warning"),
        source);
    warning_dialog = dialog;
    if (dialog)
    {
        connect(dialog, &QObject::destroyed, this, [this, dialog]()
        {
            if (warning_dialog == dialog)
                warning_dialog.clear();
        });
    }
}

void AliciaChooser::retranslate_dynamic_text()
{
    if (reset_path_button)
    {
        reset_path_button->setText(soa::i18n::translate("RESET LAUNCHER SETTINGS"));
        reset_path_button->setAccessibleName(soa::i18n::translate("Reset launcher settings"));
        reset_path_button->setToolTip(reset_path_button->isEnabled()
            ? soa::i18n::translate(
                  "Reset launcher settings and sign-in without deleting the shared prefix or either game")
            : soa::i18n::translate("Launcher settings cannot be reset while Alicia is active"));
    }
    if (download_button)
    {
        soa::ui::simple_utils::set_button_text(
            download_button, current_stage == Stage::NeedsUpdate
                ? QStringLiteral("UPDATE AVAILABLE")
                : QStringLiteral("DOWNLOAD GAME"));
    }
    if (discord_button)
        soa::ui::simple_utils::set_button_text(discord_button, QStringLiteral("PROCEED WITH DISCORD"));
    if (enter_button)
        soa::ui::simple_utils::set_button_text(enter_button, QStringLiteral("ENTER THE PLAYTEST"));
    if (signed_bug_label)
    {
        const QString text = soa::i18n::translate(
            "I understand the game has bugs and is not the final version.");
        signed_bug_label->setText(text);
        signed_bug_label->setToolTip(text);
        signed_bug_checkbox->setAccessibleName(
            soa::i18n::translate("Playtest status acknowledged"));
        signed_bug_checkbox->setAccessibleDescription(text);
        fit_acknowledgement_label(signed_bug_label, text, window()->size());
    }
    if (signed_rules_label)
    {
        const QString link_text = soa::i18n::translate("server rules").toHtmlEscaped();
        const QString link = QStringLiteral(
            "<a href=\"https://docs.google.com/document/d/1vry3ZuDtzdS_mX1P2udWlb8z2Q9Atr3p1THZdtZ2EHA/edit\" "
            "style=\"color:#2FB4E0; text-decoration:none; font-weight:700;\">%1</a>")
            .arg(link_text);
        const QString source_text = soa::i18n::translate(
            "I have read and will obey the %1.");
        const QString plain_link_text = soa::i18n::translate("server rules");
        const QString plain_text = source_text.arg(plain_link_text);
        signed_rules_label->setText(source_text.arg(link));
        signed_rules_label->setToolTip(soa::i18n::translate(
            "Open the complete Story of Alicia server rules"));
        signed_rules_checkbox->setAccessibleName(
            soa::i18n::translate("Server rules acknowledged"));
        signed_rules_checkbox->setAccessibleDescription(plain_text);
        fit_acknowledgement_label(signed_rules_label, plain_text, window()->size());
    }
    refresh_game_text();
    refresh_keep_signed_in();
    refresh_session_banner();
}

void AliciaChooser::refresh_keep_signed_in()
{
    if (!keep_signed_button)
        return;

    const bool keep = Config::instance().keep_signed_in();
    const QSignalBlocker blocker(keep_signed_button);
    keep_signed_button->setChecked(keep);
    keep_signed_button->setToolTip(keep
        ? soa::i18n::translate("Your Discord session will be restored next time the launcher starts")
        : soa::i18n::translate("Your Discord session will only last until this launcher closes"));
}

void AliciaChooser::refresh_game_text()
{
    const bool alicia_2 = game_version == soa::common::game::GameVersion::Alicia2;

    title_label->setText(alicia_2
        ? soa::i18n::translate("STORY OF ALICIA 2.0 PLAYTEST")
        : soa::i18n::translate("PLAYTEST"));

    if (enter_button)
    {
        soa::ui::simple_utils::set_button_asset(
            enter_button,
            alicia_2 ? soa::ui::assets::Button::EnterAlicia2
                     : soa::ui::assets::Button::Enter);
    }
}

void AliciaChooser::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    const QSize w = window()->size();

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const auto card = state == State::Waiting
        ? soa::ui::assets::Image::BoxWaitingForAuth
        : soa::ui::assets::Image::BoxCard;
    painter.drawPixmap(rect(), soa::ui::assets::images[card]);

    if (state == State::Download)
        painter.drawPixmap(soa::ui::layout::alicia_chooser::message(w),
                           soa::ui::assets::images[soa::ui::assets::Image::BoxNote2]);
}

bool AliciaChooser::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == download_button)
    {
        const auto action = current_stage == Stage::NeedsUpdate
            ? soa::ui::assets::Button::UpdateAvailable
            : soa::ui::assets::Button::DownloadGame;
        const auto& button = soa::ui::assets::button(action);
        soa::ui::simple_utils::apply_button_state(event, download_button,
                                               button.normal, button.hover, button.clicked);
    }
    else if (obj == discord_button)
    {
        const auto& button = soa::ui::assets::button(soa::ui::assets::Button::Discord);
        soa::ui::simple_utils::apply_button_state(event, discord_button,
                                               button.normal, button.hover, button.clicked);
    }
    else if (obj == enter_button)
    {
        const auto enter_asset = game_version == soa::common::game::GameVersion::Alicia2
            ? soa::ui::assets::Button::EnterAlicia2
            : soa::ui::assets::Button::Enter;
        const auto& button = soa::ui::assets::button(enter_asset);
        soa::ui::simple_utils::apply_button_state(event, enter_button,
                                               button.normal, button.hover, button.clicked);
    }
    return QWidget::eventFilter(obj, event);
}
