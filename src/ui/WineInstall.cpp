#include "ui/WineInstall.hpp"
#include "ui/PrefixProgress.hpp"
#include "ui/Assets.hpp"
#include "ui/Layout.hpp"
#include "ui/Styles.hpp"
#include "ui/SimpleUtils.hpp"
#include "ui/Colors.hpp"
#include <QPainter>
#include <QApplication>
#include <QFileDialog>
#include <QGraphicsBlurEffect>
#include "i18n/LanguageManager.hpp"

#include "runtime/Shell.hpp"
#include "runtime/WineRegistry.hpp"
#include "config/Config.hpp"
#include "common/Log.hpp"
#include <spdlog/spdlog.h>

using util::config::Config;

WineInstall::WineInstall(core::wine::Shell* shell_, QWidget* parent) : ModalOverlay(parent), shell(shell_)
{
    game_path = Config::instance().wine_prefix();

    setup_close_button();
    setup_buttons();

    close_button->installEventFilter(this);
    cancel_button->installEventFilter(this);
    install_button->installEventFilter(this);

    connect(&Config::instance(), &Config::changed, this, [this]()
    {
        if (!installing)
            refresh_prefix_path();
    });
}

void WineInstall::refresh_prefix_path()
{
    game_path = Config::instance().wine_prefix();
    warn_message.clear();
    update();
}

void WineInstall::setup_close_button()
{
    const QSize w = window()->size();
    close_button = util::simple_utils::make_flat_button(this);

    close_button->setAccessibleName(QStringLiteral("Close prefix installation"));
    close_button->setIcon(QIcon(util::assets::images[util::assets::Image::CloseSettings]));
    close_button->setIconSize(util::layout::install_modal::close_icon(w));
    close_button->setGeometry(util::layout::install_modal::close(w));

    connect(close_button, &QPushButton::clicked, this, [this]()
    {
        hide();
        emit closed();
    });

    close_button->raise();
}

void WineInstall::setup_buttons()
{
    const QSize w = window()->size();

    cancel_button = util::simple_utils::make_flat_button(this);
    cancel_button->setIcon(QIcon(util::assets::button(util::assets::Button::Cancel).normal));
    const QRect cancel_rect = util::layout::install_modal::cancel_button(w);
    cancel_button->setIconSize(cancel_rect.size());
    cancel_button->setGeometry(cancel_rect);
    QFont cancel_font = util::assets::fonts[util::assets::Font::EurostileExtraBlack];
    cancel_font.setPixelSize(util::layout::scaled(12, w));
    cancel_font.setWeight(QFont::Black);
    util::simple_utils::add_button_text(cancel_button, util::assets::Button::Cancel, QStringLiteral("CANCEL"), cancel_font);
    cancel_button->setAccessibleName(QStringLiteral("Cancel prefix installation"));

    connect(cancel_button, &QPushButton::clicked, this, [this]()
    {
        hide();
        emit closed();
    });

    install_button = util::simple_utils::make_flat_button(this);
    install_button->setIcon(QIcon(util::assets::button(util::assets::Button::Install).normal));
    const QRect install_rect = util::layout::install_modal::install_button(w);
    install_button->setIconSize(install_rect.size());
    install_button->setGeometry(install_rect);
    QFont install_font = util::assets::fonts[util::assets::Font::EurostileExtraBlack];
    install_font.setPixelSize(util::layout::scaled(12, w));
    install_font.setWeight(QFont::Black);
    util::simple_utils::add_button_text(install_button, util::assets::Button::Install, QStringLiteral("INSTALL"), install_font);
    install_button->setAccessibleName(QStringLiteral("Install prefix"));

    connect(install_button, &QPushButton::clicked, this, [this]()
    {
        start_install();
    });

    cancel_button->raise();
    install_button->raise();

    change_path_button = new QPushButton("Change path", this);
    change_path_button->setFlat(true);
    change_path_button->setCursor(Qt::PointingHandCursor);
    change_path_button->setStyleSheet(util::styles::link_blue_lg(w));
    change_path_button->setGeometry(util::layout::install_modal::change_path_button(w));
    change_path_button->setAccessibleName(QStringLiteral("Change prefix installation path"));

    connect(change_path_button, &QPushButton::clicked, this, [this]
    {
        const QString title = QStringLiteral("Select Wine Prefix Location");
        const QString dir = QFileDialog::getExistingDirectory(
            this, util::i18n::translate(title));
        if (!dir.isEmpty())
        {
            Config::instance().set_wine_prefix(dir);
            game_path = Config::instance().wine_prefix();
            update();
        }
    });

    change_path_button->raise();
}

void WineInstall::set_installing(const bool value)
{
    installing = value;
    install_button->setEnabled(!value);
    change_path_button->setEnabled(!value);
    cancel_button->setEnabled(!value);
    close_button->setEnabled(!value);
}

void WineInstall::start_install()
{
    if (installing)
    {
        SPDLOG_WARN("install already in progress");
        return;
    }

#if !defined(Q_OS_MACOS)
    const QString wine_path = Config::instance().wine_binary();
    const core::wine::RuntimeType type = core::wine::WineRegistry::identify(wine_path);

    if (type == core::wine::RuntimeType::Proton && !core::wine::umu_available())
    {
        warn_message = "Missing umu-run. Proton requires UMU to run without Steam.";
        SPDLOG_ERROR("install blocked: umu-run not found");
        update();
        return;
    }
#endif
    warn_message.clear();

    set_installing(true);

    if (!prefix_progress)
    {
        prefix_progress = new PrefixProgress(shell, this);
    }
    prefix_progress->show_over(this);

    SPDLOG_INFO("install: setting up wine prefix");

    auto* conn = new QMetaObject::Connection;
    *conn = connect(shell, &core::wine::Shell::wine_setup_finished, this,
        [this, conn](bool ok)
        {
            disconnect(*conn);
            delete conn;

            if (ok)
                SPDLOG_INFO("install: wine prefix ready");
            else
                SPDLOG_ERROR("install: wine setup failed");

            set_installing(false);
        });

    shell->setup();
}

void WineInstall::paint_content(QPainter& painter)
{
    const QSize w = window()->size();

    const QRect box = util::layout::install_modal::box_rect(w);
    painter.drawPixmap(box, util::assets::images[util::assets::Image::BoxGameInstall]);

    QFont title_font = util::assets::fonts[util::assets::Font::EurostileBlack];
    title_font.setPixelSize(util::layout::scaled(util::layout::text::k_modal_header, w));
    title_font.setWeight(QFont::Black);
    painter.setFont(title_font);
    painter.setPen(util::colors::k_text_maroon);
    const QString title = QStringLiteral("WINE PREFIX INSTALLATION");
    painter.drawText(util::layout::install_modal::title(w), Qt::AlignCenter,
                     util::i18n::translate(title));

    QFont body_font = util::assets::fonts[util::assets::Font::Inter];
    body_font.setPixelSize(util::layout::scaled(util::layout::text::k_body, w));
    body_font.setWeight(QFont::Medium);
    painter.setFont(body_font);
    painter.setPen(util::colors::k_text_body);
    const QString description = QStringLiteral(
        "The Wine prefix will be installed in the selected directory. You can keep the default path or choose a custom one.");
    painter.drawText(
        util::layout::install_modal::body(w),
        Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
        util::i18n::translate(description));

    const QRect path_rect = util::layout::install_modal::path_field(w);
    painter.drawPixmap(path_rect, util::assets::images[util::assets::Image::InstallPath]);

    QFont caption_font = util::assets::fonts[util::assets::Font::Inter];
    caption_font.setPixelSize(util::layout::scaled(util::layout::text::k_desc, w));
    caption_font.setWeight(QFont::Normal);
    painter.setFont(caption_font);
    painter.setPen(util::colors::k_text_caption);
    painter.drawText(path_rect.adjusted(util::layout::scaled(10, w), util::layout::scaled(8, w), -util::layout::scaled(10, w), 0), Qt::AlignTop | Qt::AlignLeft, util::i18n::translate("DEFAULT INSTALLATION PATH"));

    QFont path_font = util::assets::fonts[util::assets::Font::EurostileBold];
    path_font.setPixelSize(util::layout::scaled(16, w));
    path_font.setWeight(QFont::ExtraBold);
    painter.setFont(path_font);
    painter.setPen(util::colors::k_text_maroon);
    const QRect path_text_rect = path_rect.adjusted(util::layout::scaled(14, w), util::layout::scaled(34, w), -util::layout::scaled(14, w), 0);
    const QString elided_path = painter.fontMetrics().elidedText(
        game_path, Qt::ElideMiddle, qMax(1, path_text_rect.width()));
    painter.drawText(path_text_rect, Qt::AlignTop | Qt::AlignLeft, elided_path);

    if (!warn_message.isEmpty())
    {
        QFont warn_font = util::assets::fonts[util::assets::Font::Inter];
        warn_font.setPixelSize(util::layout::scaled(util::layout::text::k_desc, w));
        warn_font.setWeight(QFont::DemiBold);
        painter.setFont(warn_font);
        painter.setPen(util::colors::k_warning);
        painter.drawText(util::layout::install_modal::warning_line(w),
                         Qt::AlignHCenter | Qt::AlignVCenter, util::i18n::translate(warn_message));
    }
}

bool WineInstall::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == cancel_button || obj == install_button)
    {
        const auto& cancel  = util::assets::button(util::assets::Button::Cancel);
        const auto& install = util::assets::button(util::assets::Button::Install);
        if (obj == cancel_button)
            util::simple_utils::apply_button_state(event, cancel_button, cancel.normal, cancel.hover, cancel.clicked);
        else
            util::simple_utils::apply_button_state(event, install_button, install.normal, install.hover, install.clicked);
    }
    return QWidget::eventFilter(obj, event);
}
