#include "ui/GameInstall.hpp"
#include "ui/DownloadProgress.hpp"
#include "ui/Assets.hpp"
#include "ui/Layout.hpp"
#include "ui/Styles.hpp"
#include "ui/SimpleUtils.hpp"
#include "ui/Colors.hpp"
#include <QPainter>
#include <QApplication>
#include <QFileDialog>
#include <QDir>
#include "i18n/LanguageManager.hpp"

#include "runtime/Shell.hpp"
#include "config/Config.hpp"
#include "common/Log.hpp"
#include <spdlog/spdlog.h>

using util::config::Config;

GameInstall::GameInstall(core::wine::Shell* shell_, QWidget* parent) : ModalOverlay(parent), shell(shell_)
{
    refresh_game_path();

    setup_close_button();
    setup_buttons();

    close_button->installEventFilter(this);
    cancel_button->installEventFilter(this);
    install_button->installEventFilter(this);

    connect(&Config::instance(), &Config::changed, this, [this]()
    {
        if (!installing)
            refresh_game_path();
    });
}

void GameInstall::refresh_game_path()
{
    game_path = Config::instance().game_install_path();
    show_warning = false;
    update();
}

void GameInstall::setup_close_button()
{
    const QSize w = window()->size();
    close_button = util::simple_utils::make_flat_button(this);

    close_button->setAccessibleName(QStringLiteral("Close game installation"));
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

void GameInstall::setup_buttons()
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
    cancel_button->setAccessibleName(QStringLiteral("Cancel game installation"));

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
    install_button->setAccessibleName(QStringLiteral("Install game"));

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
    change_path_button->setAccessibleName(QStringLiteral("Change game installation path"));

    connect(change_path_button, &QPushButton::clicked, this, [this]
    {
        const QString dir = QFileDialog::getExistingDirectory(
            this,
            util::i18n::translate("Select Game Install Location"),
            Config::instance().prefix_root());
        if (!dir.isEmpty())
        {
            Config::instance().set_game_install_path(dir);
            game_path = Config::instance().game_install_path();
            show_warning = false;
            update();
        }
    });

    change_path_button->raise();
}


void GameInstall::set_installing(const bool value)
{
    installing = value;

    util::simple_utils::set_button_loading(install_button, value);
    install_button->setEnabled(!value);
    change_path_button->setEnabled(!value);
    update();
}

bool GameInstall::path_inside_prefix() const
{
    return Config::instance().path_inside_prefix(game_path);
}

void GameInstall::start_install()
{
    if (installing)
    {
        SPDLOG_WARN("game install already in progress");
        return;
    }

    refresh_game_path();

    if (!path_inside_prefix())
    {
        SPDLOG_ERROR("game install: chosen path {} is outside prefix root {}, refusing",
                     game_path.toStdString(),
                     Config::instance().prefix_root().toStdString());
        show_warning = true;
        update();
        return;
    }

    set_installing(true);
    show_warning = false;
    update();

    SPDLOG_INFO("game install: starting download to {}", game_path.toStdString());

    if (!download)
    {
        download = new DownloadProgress(this);
        connect(download, &DownloadProgress::download_started, this, [this]()
        {
            set_installing(true);
        });
        connect(download, &DownloadProgress::download_finished, this, [this](const bool ok)
        {
            set_installing(false);
            if (ok && download)
                download->hide();
        });
        connect(download, &DownloadProgress::closed, this, [this]()
        {
            set_installing(false);
        });
    }

    download->show_over(this);
}

void GameInstall::paint_content(QPainter& painter)
{
    const QSize w = window()->size();

    const QRect box = util::layout::install_modal::box_rect(w);
    painter.drawPixmap(box, util::assets::images[util::assets::Image::BoxGameInstall]);

    QFont title_font = util::assets::fonts[util::assets::Font::EurostileBlack];
    title_font.setPixelSize(util::layout::scaled(util::layout::text::k_modal_header, w));
    title_font.setWeight(QFont::Black);
    painter.setFont(title_font);
    painter.setPen(util::colors::k_text_maroon);
    painter.drawText(util::layout::install_modal::title(w), Qt::AlignCenter,
                     util::i18n::translate("GAME INSTALLATION"));

    QFont body_font = util::assets::fonts[util::assets::Font::Inter];
    body_font.setPixelSize(util::layout::scaled(util::layout::text::k_body, w));
    body_font.setWeight(QFont::Medium);
    painter.setFont(body_font);
    painter.setPen(util::colors::k_text_body);
    const QString installDescription = QStringLiteral(
        "The game will be downloaded into the selected directory inside your Wine prefix. You can keep the default path or choose a custom one.");
    painter.drawText(
        util::layout::install_modal::body(w),
        Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
        util::i18n::translate(installDescription));

    const QRect path_rect = util::layout::install_modal::path_field(w);
    painter.drawPixmap(path_rect, util::assets::images[util::assets::Image::InstallPath]);

    QFont caption_font = util::assets::fonts[util::assets::Font::Inter];
    caption_font.setPixelSize(util::layout::scaled(util::layout::text::k_desc, w));
    caption_font.setWeight(QFont::Normal);
    painter.setFont(caption_font);
    painter.setPen(util::colors::k_text_caption);
    painter.drawText(path_rect.adjusted(util::layout::scaled(10, w), util::layout::scaled(8, w), -util::layout::scaled(10, w), 0), Qt::AlignTop | Qt::AlignLeft, util::i18n::translate("GAME INSTALLATION PATH"));

    QFont path_font = util::assets::fonts[util::assets::Font::EurostileBold];
    path_font.setPixelSize(util::layout::scaled(16, w));
    path_font.setWeight(QFont::ExtraBold);
    painter.setFont(path_font);
    painter.setPen(util::colors::k_text_maroon);
    const QString elided = painter.fontMetrics().elidedText(
        game_path, Qt::ElideMiddle, path_rect.width() - util::layout::scaled(28, w));
    painter.drawText(path_rect.adjusted(util::layout::scaled(14, w), util::layout::scaled(34, w), -util::layout::scaled(14, w), 0), Qt::AlignTop | Qt::AlignLeft, elided);

    QFont note_font = util::assets::fonts[util::assets::Font::Inter];
    note_font.setPixelSize(util::layout::scaled(util::layout::text::k_desc, w));
    note_font.setWeight(QFont::Medium);
    painter.setFont(note_font);
    painter.setPen(util::colors::k_text_caption);
    const QRect change_path_hit = util::layout::install_modal::change_path_button(w);
    const QRect disk_space_line = util::layout::install_modal::changepath_line(w)
        .adjusted(change_path_hit.width() + util::layout::scaled(8, w), 0, 0, 0);
    const QString disk_space_text = painter.fontMetrics().elidedText(
        util::i18n::translate("~ 2 GB of free disk space required."),
        Qt::ElideLeft, qMax(1, disk_space_line.width()));
    painter.drawText(disk_space_line,
                     Qt::AlignRight | Qt::AlignVCenter,
                     disk_space_text);

    if (show_warning)
    {
        QFont warn_font = util::assets::fonts[util::assets::Font::Inter];
        warn_font.setPixelSize(util::layout::scaled(util::layout::text::k_desc, w));
        warn_font.setWeight(QFont::DemiBold);
        painter.setFont(warn_font);
        painter.setPen(util::colors::k_warning);
        const QString warning = QStringLiteral(
            "The game must be installed inside the Wine prefix.");
        painter.drawText(
            util::layout::install_modal::warning_line(w),
            Qt::AlignHCenter | Qt::AlignVCenter,
            util::i18n::translate(warning));
    }
}

bool GameInstall::eventFilter(QObject* obj, QEvent* event)
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
